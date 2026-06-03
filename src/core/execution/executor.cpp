#include "executor.hpp"
#include <cstdint>
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

    // Load microcode definitions from config into a searchable map
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
    current_inst_ = DecodedInstruction();
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
        uint64_t first_unit = mem_.read(static_cast<uint32_t>(fetch_pc_));
        first_unit &= ((1ULL << unit_bits) - 1);

        uint8_t opcode = decoder_.peek_opcode(first_unit);
        int total_bits = decoder_.get_total_bits(opcode);
        units_fetched_ = (total_bits + unit_bits - 1) / unit_bits;

        uint64_t raw = first_unit;
        for (int i = 1; i < units_fetched_; ++i) {
            uint64_t next_unit =
                mem_.read(static_cast<uint32_t>(fetch_pc_ + i));
            next_unit &= ((1ULL << unit_bits) - 1);
            raw = (raw << unit_bits) | next_unit;
        }

        current_inst_.raw_bits = raw;
        state_ = ExecutionState::DECODE;
        break;
    }

    case ExecutionState::DECODE: {
        int fetched_bits = units_fetched_ * config_.data_width;

        uint64_t first_word =
            current_inst_.raw_bits >> (fetched_bits - config_.data_width);
        uint8_t opcode = decoder_.peek_opcode(first_word);

        current_inst_ = decoder_.decode(current_inst_.raw_bits, fetched_bits);

        if (!current_inst_.is_valid)
            throw std::runtime_error("Decode Error at PC " +
                                     std::to_string(regs_.get_pc()) + " -> " +
                                     current_inst_.error);

        uop_index_ = 0;
        current_uop_cycles_ = 0;
        state_ = ExecutionState::EXECUTE_UOPS;
        break;
    }

    case ExecutionState::EXECUTE_UOPS: {
        const auto &uops = microcode_map.at(current_inst_.opcode);
        if (uop_index_ < uops.size()) {
            const auto &uop = uops[uop_index_];
            int latency = 1;
            if (uop.action == "alu") {
                std::string op_name = uop.args.at("op");
                for (const auto &op : config_.alu_ops) {
                    if (op.name == op_name) {
                        latency = op.latency;
                        break;
                    }
                }
            }

            current_uop_cycles_++;

            if (current_uop_cycles_ >= latency) {
                perform_uop(uop);
                uop_index_++;
                current_uop_cycles_ = 0;
            } else {
                // Stalling...
            }
        }
        if (uop_index_ >= uops.size()) {
            state_ = ExecutionState::DONE;
        }
        break;
    }

    case ExecutionState::DONE: {
        if (regs_.get_pc() == fetch_pc_) {
            regs_.increment_pc(units_fetched_);
        }

        if (interrupt_pending_) {
            uint64_t current_sp = regs_.read(sp_idx_);
            uint64_t new_sp =
                current_sp -
                (config_.data_width / 8 > 0 ? config_.data_width / 8 : 1);

            regs_.set_sp(new_sp);
            interrupt_pending_ = false;
        }

        state_ = ExecutionState::FETCH;
        break;
    }
    }
}

void Executor::perform_uop(const MicroOp &uop) {
    if (uop.action == "copy") {
        uint64_t val = resolve_operand(uop.args.at("source"));
        write_operand(uop.args.at("dest"), val);
    } else if (uop.action == "alu") {
        uint64_t a = resolve_operand(uop.args.at("a"));
        uint64_t b =
            uop.args.count("b") ? resolve_operand(uop.args.at("b")) : 0;
        uint64_t c =
            uop.args.count("c") ? resolve_operand(uop.args.at("c")) : 0;

        int op_width = get_operand_width(uop.args.at("out"));
        auto res = alu_.execute(uop.args.at("op"), a, b, c, op_width);
        write_operand(uop.args.at("out"), res.value);

        if (uop.args.count("update_flags") &&
            uop.args.at("update_flags") == "true") {
            if (flags_idx_ != -1)
                regs_.write(flags_idx_, res.flags_register);
        }
    } else if (uop.action == "mem_read") {
        uint64_t addr = resolve_operand(uop.args.at("addr"));
        uint64_t val = mem_.read(static_cast<uint32_t>(addr));
        write_operand(uop.args.at("out"), val);
    } else if (uop.action == "mem_write") {
        uint64_t addr = resolve_operand(uop.args.at("addr"));
        uint64_t data = resolve_operand(uop.args.at("data"));
        mem_.write(static_cast<uint32_t>(addr), data);
    } else if (uop.action == "branch") {
        bool cond = true;
        if (uop.args.count("condition")) {
            std::string c_str = uop.args.at("condition");
            uint64_t f_register =
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
            uint64_t target = resolve_operand(uop.args.at("target"));
            if (uop.args.count("relative") &&
                uop.args.at("relative") == "true") {
                // Perform sign extension for relative jumps if target is an
                // immediate
                regs_.set_pc(regs_.get_pc() + target);
            } else {
                regs_.set_pc(target);
            }
        }
    } else if (uop.action == "halt") {
        halted_ = true;
    }
}

uint64_t Executor::resolve_operand(const std::string &arg) {
    if (arg.empty())
        return 0;

    // Decoded bitfields
    if (arg[0] == '@') {
        std::string token = arg.substr(1);
        if (current_inst_.regs.count(token))
            return regs_.read(current_inst_.regs.at(token));
        if (current_inst_.imms.count(token))
            return current_inst_.imms.at(token);
        return 0;
    }

    // Special Registers / Roles
    if (arg[0] == '$') {
        std::string reg = arg.substr(1);
        if (reg == "PC")
            return regs_.get_pc();
        if (reg == "SP")
            return regs_.read(sp_idx_);
        if (reg == "FLAGS")
            return regs_.read(flags_idx_);
        if (reg == "NEXT_PC")
            return regs_.get_pc() + units_fetched_;
        return regs_.read(reg);
    }

    // Literals
    if (arg[0] == '#') {
        std::string val = arg.substr(1);
        if (val == "WORD_SIZE")
            return config_.data_width / 8 > 0 ? config_.data_width / 8 : 1;
        return std::stoull(val, nullptr, 0);
    }

    return 0;
}

void Executor::write_operand(const std::string &arg, uint64_t value) {
    if (arg.empty())
        return;

    if (arg[0] == '@') {
        std::string token = arg.substr(1);
        if (current_inst_.regs.count(token))
            regs_.write(current_inst_.regs.at(token), value);
    } else if (arg[0] == '$') {
        std::string reg = arg.substr(1);
        if (reg == "PC")
            regs_.set_pc(value);
        else if (reg == "SP")
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
