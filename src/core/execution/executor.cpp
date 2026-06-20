#include "executor.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <unordered_map>

static std::unordered_map<uint8_t, std::vector<MicroOp>> microcode_map;

Executor::Executor(Config &config, RegisterFile &regs, Memory &mem, ALU &alu)
    : config_(config), regs_(regs), mem_(mem), alu_(alu), decoder_(config) {

    pc_idx_ = regs_.find_by_role("program_counter");
    sp_idx_ = regs_.find_by_role("stack_pointer");
    flags_idx_ = regs_.find_by_role("status_flags");

    if (pc_idx_ == -1)
        throw std::runtime_error("Executor: PC register role not found.");

    for (const auto &inst : config_.instructions) {
        if (inst.microcode.empty()) {
            throw std::runtime_error("Instruction " + inst.name +
                                     " has no microcode!");
        }
        microcode_map[inst.opcode] = inst.microcode;
    }

    reset();
}

void Executor::reset() {
    state_ = ExecutionState::FETCH;
    uop_index_ = 0;
    units_fetched_ = 0;
    halted_ = false;
    cycles_ = 0;
    current_uop_cycles_ = 0;
    current_execution_cycles_ = 0;
    current_inst_ = DecodedInstruction();

    interrupt_pending_ = false;
    pending_interrupt_id_ = -1;
    pc_modified_ = false;

    last_data_val_ = 0;
    last_addr_val_ = 0;
    last_alu_a_ = 0;
    last_alu_b_ = 0;
    last_alu_out_ = 0;
}

void Executor::step_uop() {
    if (halted_)
        return;

    cycles_++;

    switch (state_) {
    case ExecutionState::FETCH: {
        fetch_pc_ = regs_.get_pc();
        current_inst_ = DecodedInstruction();

        int unit_bits = config_.data_width;
        int unit_bytes = (config_.data_width + 7) / 8;

        word_t first_unit = mem_.read(fetch_pc_, true);
        first_unit &= mask_for_width(unit_bits);

        uint8_t opcode = decoder_.peek_opcode(first_unit);
        int total_bits = decoder_.get_total_bits(opcode);
        units_fetched_ = (total_bits + unit_bits - 1) / unit_bits;

        word_t raw = first_unit;
        for (int i = 1; i < units_fetched_; ++i) {
            word_t next_unit =
                mem_.read(fetch_pc_ + static_cast<word_t>(i * unit_bytes), true);
            next_unit &= mask_for_width(unit_bits);
            raw = (raw << unit_bits) | next_unit;
        }

        current_inst_.raw_bits = raw;
        last_addr_val_ = fetch_pc_;
        last_data_val_ = raw;

        state_ = ExecutionState::DECODE;
        break;
    }

    case ExecutionState::DECODE: {
        int fetched_bits = units_fetched_ * config_.data_width;

        word_t first_word =
            current_inst_.raw_bits >> (fetched_bits - config_.data_width);
        uint8_t opcode = decoder_.peek_opcode(first_word);

        current_inst_ = decoder_.decode(current_inst_.raw_bits, fetched_bits);

        if (!current_inst_.is_valid)
            throw std::runtime_error("Decode Error at PC " +
                                     word_to_dec_string(regs_.get_pc()) +
                                     " -> " +
                                     current_inst_.error);

        uop_index_ = 0;
        current_uop_cycles_ = 0;
        current_execution_cycles_ = 0;

        // Perform visual pipeline lookahead resolution before entering
        // execution state
        pre_resolve_diagnostics();

        state_ = ExecutionState::EXECUTE_UOPS;
        break;
    }

    case ExecutionState::EXECUTE_UOPS: {
        const auto &uops = microcode_map.at(current_inst_.opcode);
        current_execution_cycles_++;

        int target_latency = -1;
        LatencyMode mode = LatencyMode::DYNAMIC;
        for (const auto &inst : config_.instructions) {
            if (inst.opcode == current_inst_.opcode) {
                target_latency = inst.execution_latency;
                mode = inst.latency_mode;
                break;
            }
        }

        // STRICT Mode
        if (mode == LatencyMode::STRICT) {
            int cycles_left = target_latency - (current_execution_cycles_ - 1);

            if (cycles_left <= 1) {
                while (uop_index_ < uops.size()) {
                    perform_uop(uops[uop_index_]);
                    uop_index_++;
                }
                state_ = ExecutionState::DONE;
            } else {
                int uops_left = static_cast<int>(uops.size()) -
                                static_cast<int>(uop_index_);
                if (uops_left > 0) {
                    int uops_to_run =
                        (uops_left + cycles_left - 1) / cycles_left;
                    for (int i = 0; i < uops_to_run && uop_index_ < uops.size();
                         ++i) {
                        perform_uop(uops[uop_index_]);
                        uop_index_++;
                    }
                }
            }
            if (current_execution_cycles_ >= target_latency) {
                state_ = ExecutionState::DONE;
            }
        }
        // BOTTLENECK Mode
        else if (mode == LatencyMode::BOTTLENECK) {
            if (uop_index_ < uops.size()) {
                const auto &uop = uops[uop_index_];
                int uop_latency = 1;
                if (uop.action == "alu") {
                    std::string op_name = uop.args.at("op");
                    for (const auto &op : config_.alu_ops) {
                        if (op.name == op_name) {
                            uop_latency = op.latency;
                            break;
                        }
                    }
                }

                current_uop_cycles_++;

                if (current_uop_cycles_ >= uop_latency) {
                    perform_uop(uop);
                    uop_index_++;
                    current_uop_cycles_ = 0;
                }
            }

            bool uops_finished = (uop_index_ >= uops.size());
            bool latency_met = (current_execution_cycles_ >= target_latency);

            if (uops_finished && latency_met) {
                state_ = ExecutionState::DONE;
            }
        }
        // ADDITIVE Mode
        else if (mode == LatencyMode::ADDITIVE) {
            if (uop_index_ < uops.size()) {
                const auto &uop = uops[uop_index_];
                int uop_latency = 1;
                if (uop.action == "alu") {
                    std::string op_name = uop.args.at("op");
                    for (const auto &op : config_.alu_ops) {
                        if (op.name == op_name) {
                            uop_latency = op.latency;
                            break;
                        }
                    }
                }

                current_uop_cycles_++;

                if (current_uop_cycles_ >= uop_latency) {
                    perform_uop(uop);
                    uop_index_++;
                    current_uop_cycles_ = 0;
                }
            } else {
                current_uop_cycles_++;
                if (current_uop_cycles_ >= target_latency) {
                    state_ = ExecutionState::DONE;
                }
            }
        }
        // DYNAMIC Mode (Fallback)
        else {
            if (uop_index_ < uops.size()) {
                const auto &uop = uops[uop_index_];
                int uop_latency = 1;
                if (uop.action == "alu") {
                    std::string op_name = uop.args.at("op");
                    for (const auto &op : config_.alu_ops) {
                        if (op.name == op_name) {
                            uop_latency = op.latency;
                            break;
                        }
                    }
                }

                current_uop_cycles_++;

                if (current_uop_cycles_ >= uop_latency) {
                    perform_uop(uop);
                    uop_index_++;
                    current_uop_cycles_ = 0;
                }
            }
            if (uop_index_ >= uops.size()) {
                state_ = ExecutionState::DONE;
            }
        }
        break;
    }

    case ExecutionState::DONE: {
        if (regs_.get_pc() == fetch_pc_ && !pc_modified_) {
            int unit_bytes = (config_.data_width + 7) / 8;
            regs_.increment_pc(units_fetched_ * unit_bytes);
        }

        if (interrupt_pending_) {
            if (sp_idx_ != -1) {
                word_t current_sp = regs_.read(sp_idx_);
                int addr_bytes = (config_.addr_width + 7) / 8;
                word_t new_sp = current_sp - static_cast<word_t>(addr_bytes);
                regs_.set_sp(new_sp);

                mem_.write(new_sp, regs_.get_pc(), config_.addr_width);
            }

            regs_.set_pc(pending_interrupt_id_);
            interrupt_pending_ = false;
        }

        pc_modified_ = false;

        // Pre-cache next PC address to update visualizer instantly in upcoming
        // FETCH frame
        last_addr_val_ = regs_.get_pc();

        state_ = ExecutionState::FETCH;
        break;
    }
    }
}

void Executor::perform_uop(const MicroOp &uop) {
    if (uop.action == "copy") {
        word_t val = resolve_operand(uop.args.at("source"));
        write_operand(uop.args.at("dest"), val);

        last_data_val_ = val;

    } else if (uop.action == "alu") {
        word_t a = resolve_operand(uop.args.at("a"));
        word_t b =
            uop.args.count("b") ? resolve_operand(uop.args.at("b")) : 0;
        word_t c =
            uop.args.count("c") ? resolve_operand(uop.args.at("c")) : 0;

        int op_width = get_operand_width(uop.args.at("out"));
        auto res = alu_.execute(uop.args.at("op"), a, b, c, op_width);
        write_operand(uop.args.at("out"), res.value);

        last_alu_a_ = a;
        last_alu_b_ = b;
        last_alu_out_ = res.value;
        last_data_val_ = res.value;

        if (uop.args.count("update_flags") &&
            uop.args.at("update_flags") == "true") {
            if (flags_idx_ != -1)
                regs_.write(flags_idx_, res.flags_register);
        }

    } else if (uop.action == "mem_read") {
        word_t addr = resolve_operand(uop.args.at("addr"));
        int width = get_operand_width(uop.args.at("out"));
        word_t val = mem_.read(addr, false, width);
        write_operand(uop.args.at("out"), val);

        last_addr_val_ = addr;
        last_data_val_ = val;

    } else if (uop.action == "mem_write") {
        word_t addr = resolve_operand(uop.args.at("addr"));
        word_t data = resolve_operand(uop.args.at("data"));
        int width = get_operand_width(uop.args.at("data"));
        mem_.write(addr, data, width);

        last_addr_val_ = addr;
        last_data_val_ = data;

    } else if (uop.action == "port_read") {
        word_t port = resolve_operand(uop.args.at("port"));
        int width = get_operand_width(uop.args.at("out"));
        word_t val = mem_.port_read(port);
        write_operand(uop.args.at("out"), val);

        last_addr_val_ = port;
        last_data_val_ = val;

    } else if (uop.action == "port_write") {
        word_t port = resolve_operand(uop.args.at("port"));
        word_t data = resolve_operand(uop.args.at("data"));
        mem_.port_write(port, data);

        last_addr_val_ = port;
        last_data_val_ = data;

    } else if (uop.action == "coproc_read") {
        int cp_id = static_cast<int>(resolve_operand(uop.args.at("cp")));
        int reg_id = static_cast<int>(resolve_operand(uop.args.at("reg")));
        word_t val = regs_.read_coproc(cp_id, reg_id);
        write_operand(uop.args.at("out"), val);

        last_data_val_ = val;

    } else if (uop.action == "coproc_write") {
        int cp_id = static_cast<int>(resolve_operand(uop.args.at("cp")));
        int reg_id = static_cast<int>(resolve_operand(uop.args.at("reg")));
        word_t data = resolve_operand(uop.args.at("data"));
        regs_.write_coproc(cp_id, reg_id, data);

        last_data_val_ = data;

    } else if (uop.action == "branch") {
        bool cond = true;
        if (uop.args.count("condition")) {
            std::string c_str = uop.args.at("condition");
            word_t f_register =
                (flags_idx_ != -1) ? regs_.read(flags_idx_) : 0;

            bool invert = false;
            std::string search_type = c_str;
            if (c_str[0] == '!') {
                invert = true;
                search_type = c_str.substr(1);
            }

            int bit_pos = -1;
            for (const auto &f_def : config_.alu_flags) {
                if (f_def.type == search_type) {
                    bit_pos = f_def.bit;
                    break;
                }
            }

            if (bit_pos != -1) {
                bool is_set = (f_register >> bit_pos) & 1;
                cond = invert ? !is_set : is_set;
            } else {
                for (const auto &f_def : config_.alu_flags) {
                    if (f_def.name == search_type) {
                        bit_pos = f_def.bit;
                        break;
                    }
                }
                if (bit_pos != -1) {
                    bool is_set = (f_register >> bit_pos) & 1;
                    cond = invert ? !is_set : is_set;
                } else {
                    throw std::runtime_error(
                        "Branch condition error: Flag type/name '" +
                        search_type + "' not found in config.");
                }
            }
        }

        if (cond) {
            word_t target = resolve_operand(uop.args.at("target"));
            last_addr_val_ = target;
            if (uop.args.count("relative") &&
                uop.args.at("relative") == "true") {
                regs_.set_pc(regs_.get_pc() + target);
            } else {
                regs_.set_pc(target);
            }
            pc_modified_ = true;
        }
    } else if (uop.action == "halt") {
        halted_ = true;
    }
}

void Executor::pre_resolve_diagnostics() {
    const auto &uops = microcode_map.at(current_inst_.opcode);
    if (uops.empty())
        return;

    const auto &uop = uops[0];

    if (uop.action == "copy") {
        word_t val = uop.args.count("source")
                        ? resolve_operand(uop.args.at("source"))
                        : 0;
        last_data_val_ = val;

    } else if (uop.action == "alu") {
        word_t a =
            uop.args.count("a") ? resolve_operand(uop.args.at("a")) : 0;
        word_t b =
            uop.args.count("b") ? resolve_operand(uop.args.at("b")) : 0;
        word_t c =
            uop.args.count("c") ? resolve_operand(uop.args.at("c")) : 0;

        int op_width = uop.args.count("out")
                           ? get_operand_width(uop.args.at("out"))
                           : config_.data_width;
        if (uop.args.count("op")) {
            auto res = alu_.execute(uop.args.at("op"), a, b, c, op_width);
            last_alu_a_ = a;
            last_alu_b_ = b;
            last_alu_out_ = res.value;
            last_data_val_ = res.value;
        }

    } else if (uop.action == "mem_read") {
        word_t addr =
            uop.args.count("addr") ? resolve_operand(uop.args.at("addr")) : 0;
        last_addr_val_ = addr;

        int width = uop.args.count("out")
                        ? get_operand_width(uop.args.at("out"))
                        : config_.data_width;
        word_t val = mem_.read(addr, false, width);
        last_data_val_ = val;

    } else if (uop.action == "mem_write") {
        word_t addr =
            uop.args.count("addr") ? resolve_operand(uop.args.at("addr")) : 0;
        word_t data =
            uop.args.count("data") ? resolve_operand(uop.args.at("data")) : 0;

        last_addr_val_ = addr;
        last_data_val_ = data;

    } else if (uop.action == "branch") {
        word_t target = uop.args.count("target")
                           ? resolve_operand(uop.args.at("target"))
                           : 0;
        last_addr_val_ = target;
    }
}

word_t Executor::resolve_operand(const std::string &arg) {
    if (arg.empty())
        return 0;

    if (arg[0] == '@') {
        std::string token = arg.substr(1);
        if (current_inst_.regs.count(token))
            return regs_.read(current_inst_.regs.at(token));
        if (current_inst_.imms.count(token))
            return current_inst_.imms.at(token);
        return 0;
    }

    if (arg[0] == '$') {
        std::string reg = arg.substr(1);
        if (reg == "PC")
            return regs_.get_pc();
        if (reg == "SP")
            return (sp_idx_ != -1) ? regs_.read(sp_idx_) : 0;
        if (reg == "FLAGS")
            return (flags_idx_ != -1) ? regs_.read(flags_idx_) : 0;
        if (reg == "NEXT_PC") {
            int unit_bytes = (config_.data_width + 7) / 8;
            return regs_.get_pc() +
                  static_cast<word_t>(units_fetched_ * unit_bytes);
        }
        return regs_.read(reg);
    }

    if (arg[0] == '#') {
        std::string val = arg.substr(1);
        if (val == "WORD_SIZE")
            return config_.data_width / 8 > 0 ? config_.data_width / 8 : 1;
        if (val == "ADDR_SIZE")
            return (config_.addr_width + 7) / 8;
        return parse_word(val);
    }

    return 0;
}

void Executor::write_operand(const std::string &arg, word_t value) {
    if (arg.empty())
        return;

    if (arg[0] == '@') {
        std::string token = arg.substr(1);
        if (current_inst_.regs.count(token))
            regs_.write(current_inst_.regs.at(token), value);
    } else if (arg[0] == '$') {
        std::string reg = arg.substr(1);
        if (reg == "PC") {
            regs_.set_pc(value);
            pc_modified_ = true;
        } else if (reg == "SP")
            regs_.write(sp_idx_, value);
        else if (reg == "FLAGS")
            regs_.write(flags_idx_, value);
        else
            regs_.write(reg, value);
    }
}

int Executor::get_operand_width(const std::string &arg) {
    if (arg.empty())
        return config_.data_width;

    if (arg[0] == '@') {
        std::string token = arg.substr(1);
        if (current_inst_.regs.count(token)) {
            int reg_idx = current_inst_.regs.at(token);
            if (reg_idx >= 0 &&
                reg_idx < static_cast<int>(regs_.get_defs().size())) {
                return regs_.get_defs()[reg_idx].width;
            }
        }
    } else if (arg[0] == '$') {
        std::string reg = arg.substr(1);
        if (reg == "PC") {
            if (pc_idx_ != -1)
                return regs_.get_defs()[pc_idx_].width;
        } else if (reg == "SP") {
            if (sp_idx_ != -1)
                return regs_.get_defs()[sp_idx_].width;
        } else if (reg == "FLAGS") {
            if (flags_idx_ != -1)
                return regs_.get_defs()[flags_idx_].width;
        } else if (reg == "NEXT_PC") {
            if (pc_idx_ != -1)
                return regs_.get_defs()[pc_idx_].width;
        } else {
            for (const auto &def : regs_.get_defs()) {
                if (def.name == reg) {
                    return def.width;
                }
            }
        }
    }
    return config_.data_width;
}

int Executor::get_current_uop_latency() const {
    if (state_ != ExecutionState::EXECUTE_UOPS) {
        return 1;
    }

    const auto &uops = microcode_map.at(current_inst_.opcode);

    if (uop_index_ < uops.size()) {
        const auto &uop = uops[uop_index_];
        if (uop.action == "alu") {
            std::string op_name = uop.args.at("op");
            for (const auto &op : config_.alu_ops) {
                if (op.name == op_name)
                    return op.latency;
            }
        }
    }

    return 1;
}
