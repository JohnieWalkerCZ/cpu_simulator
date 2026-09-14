#include "config.hpp"
#include "alu/alu.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

// nlohmann::json's get<T>() has no __int128 overload, and JSON numbers lose
// precision above 64 bits anyway -- values that need the full word_t range
// must be supplied as a quoted hex/dec/binary string in the config.
static word_t json_to_word(const nlohmann::json &val) {
    if (val.is_string())
        return parse_word(val.get<std::string>());
    return static_cast<word_t>(val.get<uint64_t>());
}

static bool valid_flag_expression(const std::string &expression) {
    std::string normalized;
    for (size_t i = 0; i < expression.size();) {
        if (std::isalpha(static_cast<unsigned char>(expression[i])) || expression[i] == '_') {
            size_t end = i + 1;
            while (end < expression.size() &&
                   (std::isalnum(static_cast<unsigned char>(expression[end])) || expression[end] == '_'))
                ++end;
            const std::string name = expression.substr(i, end - i);
            if (name != "a" && name != "b" && name != "c" && name != "res" && name != "max_val")
                return false;
            normalized += '1';
            i = end;
        } else {
            normalized += expression[i++];
        }
    }
    return ALU::is_valid_expression(normalized);
}

static void parse_mask_pattern(const nlohmann::json &val, word_t &pattern,
                               int &pattern_len, int reg_width) {
    if (val.is_number()) {
        pattern = json_to_word(val);
        pattern_len = reg_width;
        return;
    }
    std::string str = val.get<std::string>();
    if (str.compare(0, 2, "0b") == 0 || str.compare(0, 2, "0B") == 0) {
        std::string pat_str = str.substr(2);
        pattern_len = static_cast<int>(pat_str.size());
        pattern = parse_word("0b" + pat_str);
    } else if (str.compare(0, 2, "0x") == 0 || str.compare(0, 2, "0X") == 0) {
        pattern_len = static_cast<int>((str.size() - 2) * 4);
        pattern = parse_word(str);
    } else {
        pattern_len = 8;
        pattern = parse_word(str);
    }

    if (pattern_len > reg_width) {
        pattern_len = reg_width;
        pattern &= mask_for_width(pattern_len);
    }
}

static void parse_register_recursive(const nlohmann::json &j_reg, Config &cfg,
                                     int &next_phys_idx, int parent_phys_idx,
                                     int parent_reg_idx,
                                     const std::string &parent_role,
                                     const std::vector<int> &parent_mapping) {
    RegisterDef reg;
    reg.name = j_reg.at("name").get<std::string>();
    reg.width = j_reg.at("width").get<int>();
    if (reg.width < 1 || reg.width > 128)
        throw std::runtime_error("Register '" + reg.name + "' has invalid width");
    reg.is_coproc = j_reg.value("coproc", false);
    reg.coproc_id = j_reg.value("coproc_id", -1);
    reg.coproc_reg_id = j_reg.value("coproc_reg_id", -1);
    reg.parent_index = parent_reg_idx;

    if (j_reg.contains("initial")) {
        reg.initial = json_to_word(j_reg["initial"]);
    } else {
        reg.initial = 0;
    }

    reg.role = j_reg.value("role", "");

    if (parent_phys_idx == -1) {
        reg.is_alias = false;
        reg.physical_index = next_phys_idx++;
        reg.bit_mapping.resize(reg.width);
        for (int i = 0; i < reg.width; ++i) {
            reg.bit_mapping[i] = i;
        }
    } else {
        reg.is_alias = true;
        reg.physical_index = parent_phys_idx;
        if (reg.role.empty()) {
            reg.role = parent_role;
        }

        if (j_reg.contains("mask")) {
            word_t pattern = 0;
            int pattern_len = 0;
            parse_mask_pattern(j_reg["mask"], pattern, pattern_len, reg.width);

            int parent_width = static_cast<int>(parent_mapping.size());
            std::vector<int> active_indices;
            for (int i = 0; i < parent_width; ++i) {
                int bit_idx = i % pattern_len;
                if ((pattern >> bit_idx) & 1) {
                    active_indices.push_back(i);
                }
            }

            // positions
            reg.bit_mapping.resize(reg.width);
            for (int i = 0; i < reg.width; ++i) {
                if (i < static_cast<int>(active_indices.size())) {
                    reg.bit_mapping[i] = parent_mapping[active_indices[i]];
                } else {
                    reg.bit_mapping[i] = -1;
                }
            }
        } else {
            int offset = j_reg.value("offset", 0);
            if (offset < 0)
                throw std::runtime_error("Register '" + reg.name + "' has a negative offset");
            reg.bit_mapping.resize(reg.width);
            for (int i = 0; i < reg.width; ++i) {
                int parent_idx = offset + i;
                if (parent_idx < static_cast<int>(parent_mapping.size())) {
                    reg.bit_mapping[i] = parent_mapping[parent_idx];
                } else {
                    reg.bit_mapping[i] = -1;
                }
            }
        }
    }

    cfg.registers.push_back(reg);
    int current_reg_idx = static_cast<int>(cfg.registers.size()) - 1;

    if (j_reg.contains("sub_registers")) {
        for (const auto &child : j_reg["sub_registers"]) {
            parse_register_recursive(child, cfg, next_phys_idx,
                                     reg.physical_index, current_reg_idx,
                                     reg.role, reg.bit_mapping);
        }
    }
}

Config Config::from_json(const nlohmann::json &j) {
    Config cfg;

    cfg.name = j.at("name").get<std::string>();
    cfg.data_width = j["data_bus"].at("width").get<int>();
    cfg.addr_width = j["address_bus"].at("width").get<int>();
    cfg.opcode_width = j.value("instruction_set", nlohmann::json::object())
                           .value("opcode_width", cfg.data_width >= 8 ? 8 : cfg.data_width);
    cfg.memory_size = json_to_word(j["memory"].at("size"));
    cfg.endianness = j["memory"].value("endianness", "little");
    cfg.memory_architecture = j["memory"].value("architecture", "von_neumann");

    if (j["memory"].contains("segments")) {
        for (const auto &seg : j["memory"]["segments"]) {
            MemorySegmentDef def;
            def.name = seg.at("name").get<std::string>();
            def.start = json_to_word(seg.at("start"));
            def.end = json_to_word(seg.at("end"));
            def.r = seg.value("R", true);
            def.w = seg.value("W", true);
            def.x = seg.value("X", true);
            cfg.memory_segments.push_back(def);
        }
    } else {
        cfg.memory_segments.push_back(
            {"FLAT_RAM", 0, cfg.memory_size - 1, true, true, true});
    }

    int next_phys_idx = 0;
    if (j.contains("registers")) {
        const auto &regs = j["registers"];
        if (regs.contains("general_purpose")) {
            for (const auto &r : regs["general_purpose"]) {
                parse_register_recursive(r, cfg, next_phys_idx, -1, -1, "", {});
            }
        }
        if (regs.contains("special")) {
            for (const auto &r : regs["special"]) {
                parse_register_recursive(r, cfg, next_phys_idx, -1, -1,
                                         r.value("role", ""), {});
            }
        }
    }

    if (j.contains("alu")) {
        const auto &alu_json = j["alu"];

        if (alu_json.contains("flags")) {
            for (const auto &f : alu_json["flags"]) {
                FlagDef flag;
                flag.name = f.at("name").get<std::string>();
                flag.bit = f.at("bit").get<int>();
                flag.type = f.value("type", "");
                flag.expression = f.value("expression", "");
                cfg.alu_flags.push_back(flag);
            }
        }

        if (alu_json.contains("operations")) {
            for (const auto &op : alu_json["operations"]) {
                ALUOp alu_op;
                alu_op.name = op.at("name").get<std::string>();
                alu_op.code = op.value("code", (uint8_t)0);
                alu_op.expression = op.at("expression").get<std::string>();
                alu_op.latency = op.value("latency", 1);

                if (op.contains("flag_rules")) {
                    for (auto &[flag_name, logic_type] :
                         op["flag_rules"].items()) {
                        alu_op.flag_rules[flag_name] =
                            logic_type.get<std::string>();
                    }
                }
                cfg.alu_ops.push_back(alu_op);
            }
        }
    }

    if (j.contains("instruction_set") &&
        j["instruction_set"].contains("instructions")) {
        for (const auto &inst : j["instruction_set"]["instructions"]) {
            Instruction ins;
            ins.name = inst.at("name").get<std::string>();
            ins.opcode = inst.at("opcode").get<uint16_t>();
            ins.execution_latency = inst.value("latency", -1);

            std::string mode_str = inst.value("latency_mode", "dynamic");
            if (ins.execution_latency > 0) {
                if (mode_str == "strict" || mode_str == "fixed") {
                    ins.latency_mode = LatencyMode::STRICT;
                } else if (mode_str == "bottleneck" || mode_str == "max") {
                    ins.latency_mode = LatencyMode::BOTTLENECK;
                } else if (mode_str == "additive") {
                    ins.latency_mode = LatencyMode::ADDITIVE;
                } else {
                    ins.latency_mode = LatencyMode::BOTTLENECK;
                }
            } else {
                ins.latency_mode = LatencyMode::DYNAMIC;
            }

            if (inst.contains("encoding")) {
                for (const auto &enc : inst["encoding"]) {
                    if (enc.is_number())
                        ins.encoding.push_back(enc.get<int>());
                    else if (enc.is_string()) {
                        std::string token = enc.get<std::string>();
                        if (token == "dest")
                            ins.encoding.push_back(-1);
                        else if (token == "src")
                            ins.encoding.push_back(-2);
                        else if (token == "addr_reg")
                            ins.encoding.push_back(-3);
                        else if (token == "offset")
                            ins.encoding.push_back(-4);
                        else if (token == "imm8")
                            ins.encoding.push_back(-5);
                        else if (token == "imm16")
                            ins.encoding.push_back(-6);
                        else if (token == "address")
                            ins.encoding.push_back(-7);
                        else if (token == "imm")
                            ins.encoding.push_back(-5);
                        else
                            ins.encoding.push_back(-10);
                    }
                }
            }

            if (inst.contains("microcode")) {
                for (const auto &uop_json : inst["microcode"]) {
                    MicroOp uop;
                    uop.action = uop_json.at("action").get<std::string>();

                    for (auto it = uop_json.begin(); it != uop_json.end();
                         ++it) {
                        if (it.key() != "action") {
                            if (it.value().is_string()) {
                                uop.args[it.key()] =
                                    it.value().get<std::string>();
                            } else if (it.value().is_number()) {
                                uop.args[it.key()] =
                                    std::to_string(it.value().get<int>());
                            } else if (it.value().is_boolean()) {
                                uop.args[it.key()] =
                                    it.value().get<bool>() ? "true" : "false";
                            }
                        }
                    }
                    ins.microcode.push_back(uop);
                }
            }
            cfg.instructions.push_back(ins);
        }
    }

    if (j.contains("peripherals")) {
        for (const auto &p : j["peripherals"]) {
            PeripheralDef def;
            def.name = p.at("name").get<std::string>();
            def.type = p.at("type").get<std::string>();

            if (p.contains("address")) {
                def.address_start = json_to_word(p.at("address"));
                def.address_end = def.address_start;
            } else {
                def.address_start = json_to_word(p.at("address_start"));
                def.address_end = json_to_word(p.at("address_end"));
            }

            if (p.contains("parameters")) {
                for (auto &[key, val] : p["parameters"].items()) {
                    if (val.is_string())
                        def.parameters[key] = val.get<std::string>();
                    else if (val.is_number_integer() || val.is_number_unsigned())
                        def.parameters[key] = word_to_dec_string(json_to_word(val));
                    else
                        throw std::runtime_error("Peripheral parameter must be a string or integer");
                }
            }

            if (p.contains("internal_state")) {
                for (auto &[key, val] : p["internal_state"].items()) {
                    def.internal_state[key] =
                        (val.is_number() || val.is_string())
                            ? json_to_word(val)
                            : 0;
                }
            }

            if (p.contains("registers")) {
                for (const auto &r : p["registers"]) {
                    PeripheralRegisterDef rdef;
                    rdef.name = r.at("name").get<std::string>();
                    rdef.offset = r.at("offset").get<int>();
                    rdef.size_bytes = r.value("size_bytes", 1);
                    rdef.access = r.value("access", "rw");
                    rdef.initial = r.contains("initial")
                                      ? json_to_word(r["initial"])
                                      : 0;

                    if (r.contains("on_read"))
                        rdef.on_read = r["on_read"];
                    if (r.contains("on_write"))
                        rdef.on_write = r["on_write"];
                    def.registers.push_back(rdef);
                }
            }

            if (p.contains("tick_behavior")) {
                def.tick_behavior = p["tick_behavior"];
            }

            cfg.peripherals.push_back(def);
        }
    }

    return cfg;
}

Config Config::from_file(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open: " + path);
    nlohmann::json j;
    f >> j;
    return from_json(j);
}

bool Config::validate() const {
    std::unordered_set<std::string> names;
    std::unordered_set<uint16_t> opcodes;
    std::unordered_set<uint8_t> alu_codes;

    auto role_count = [&](const std::string &a, const std::string &b) {
        int count = 0;
        for (const auto &reg : registers)
            count += reg.role == a || reg.role == b;
        return count;
    };
    auto has_arg = [](const MicroOp &uop, const char *name) {
        return uop.args.contains(name) && !uop.args.at(name).empty();
    };
    auto is_known_register = [&](const std::string &name) {
        return std::any_of(registers.begin(), registers.end(), [&](const RegisterDef &reg) {
            return reg.name == name;
        });
    };

    if (data_width < 4 || data_width > 128 || (data_width % 4) != 0) {
        return false;
    }

    if (addr_width < 4 || addr_width > 128 || (addr_width % 4) != 0) {
        return false;
    }
    if (opcode_width < 1 || opcode_width > 16 || opcode_width > data_width)
        return false;

    word_t max_adressable = (addr_width >= 128)
                                ? ~static_cast<word_t>(0)
                                : (static_cast<word_t>(1) << addr_width);
    if (memory_size == 0 || memory_size > max_adressable) {
        return false;
    }
    if (endianness != "little" && endianness != "big")
        return false;
    if (memory_architecture != "von_neumann" && memory_architecture != "harvard")
        return false;

    for (const RegisterDef &reg : registers) {
        if (names.count(reg.name))
            return false;

        names.insert(reg.name);

        if (reg.width < 1 || reg.width > 128)
            return false;

        word_t max_val = mask_for_width(reg.width);
        if (reg.initial > max_val)
            return false;

        if (!reg.role.empty()) {
            if (reg.role != "pc" && reg.role != "sp" && reg.role != "flags" &&
                reg.role != "ir" && reg.role != "accumulator" &&
                reg.role != "program_counter" && reg.role != "stack_pointer" &&
                reg.role != "status_flags")
                return false;
        }
        if (reg.physical_index < 0)
            return false;
        for (int bit : reg.bit_mapping) {
            if (bit < 0 || bit >= 128)
                return false;
        }
    }

    if (role_count("pc", "program_counter") != 1 ||
        role_count("sp", "stack_pointer") > 1 ||
        role_count("flags", "status_flags") > 1)
        return false;

    int flags_width = 0;
    for (const auto &reg : registers)
        if (reg.role == "flags" || reg.role == "status_flags")
            flags_width = reg.width;
    std::unordered_set<std::string> flag_names;
    std::unordered_set<int> flag_bits;
    for (const auto &flag : alu_flags) {
        if (flag.bit < 0 || flag.bit >= flags_width || !flag_names.insert(flag.name).second ||
            !flag_bits.insert(flag.bit).second ||
            (!flag.expression.empty() && !valid_flag_expression(flag.expression)))
            return false;
    }

    std::unordered_set<std::string> alu_names;
    for (const ALUOp &op : alu_ops) {
        if (alu_codes.count(op.code) || !alu_names.insert(op.name).second || op.name.empty())
            return false;
        alu_codes.insert(op.code);

        if (op.latency < 1 || !ALU::is_valid_expression(op.expression))
            return false;
    }

    int reg_bits = registers.size() <= 1 ? 1 : 0;
    while ((static_cast<size_t>(1) << reg_bits) < registers.size())
        ++reg_bits;
    const int opcode_bits = opcode_width;
    std::unordered_set<std::string> instruction_names;
    for (const auto &inst : instructions) {
        if (!instruction_names.insert(inst.name).second || opcodes.count(inst.opcode) ||
            inst.encoding.empty() || inst.microcode.empty() ||
            inst.opcode > mask_for_width(opcode_bits))
            return false;
        opcodes.insert(inst.opcode);
        if (inst.encoding.front() != inst.opcode)
            return false;
        int total_bits = 0;
        for (size_t i = 0; i < inst.encoding.size(); ++i) {
            const int field = inst.encoding[i];
            if (field >= 0) {
                if (field > static_cast<int>(mask_for_width(i == 0 ? opcode_bits : 4)))
                    return false;
                total_bits += i == 0 ? opcode_bits : 4;
            } else if (field >= -3) {
                total_bits += reg_bits;
            } else if (field == -4 || field == -5 || field == -10) {
                if (field == -10)
                    return false;
                total_bits += 8;
            } else if (field == -6) {
                total_bits += 16;
            } else if (field == -7) {
                total_bits += addr_width;
            } else {
                return false;
            }
        }
        const int fetched_bits = ((total_bits + data_width - 1) / data_width) * data_width;
        if (fetched_bits > 128)
            return false;
        std::unordered_set<std::string> decoded_operands;
        for (int field : inst.encoding) {
            if (field == -1) decoded_operands.insert("dest");
            else if (field == -2) decoded_operands.insert("src");
            else if (field == -3) decoded_operands.insert("addr_reg");
            else if (field == -4) decoded_operands.insert("offset");
            else if (field == -5) decoded_operands.insert("imm8");
            else if (field == -6) decoded_operands.insert("imm16");
            else if (field == -7) decoded_operands.insert("address");
        }
        auto valid_operand = [&](const std::string &arg, bool writable) {
            if (arg.empty()) return false;
            if (arg[0] == '@') {
                const std::string token = arg.substr(1);
                if (!decoded_operands.contains(token)) return false;
                return !writable || token == "dest" || token == "src" || token == "addr_reg";
            }
            if (arg[0] == '$') {
                const std::string name = arg.substr(1);
                return name == "PC" || name == "SP" || name == "FLAGS" ||
                       (!writable && name == "NEXT_PC") || is_known_register(name);
            }
            if (writable) return false;
            if (arg[0] != '#') return false;
            const std::string value = arg.substr(1);
            if (value == "WORD_SIZE" || value == "ADDR_SIZE") return true;
            try { parse_word(value); return true; } catch (...) { return false; }
        };
        for (const auto &uop : inst.microcode) {
            if (uop.action == "copy") {
                if (!has_arg(uop, "source") || !has_arg(uop, "dest")) return false;
            } else if (uop.action == "alu") {
                if (!has_arg(uop, "op") || !has_arg(uop, "a") || !has_arg(uop, "out") ||
                    !alu_names.contains(uop.args.at("op"))) return false;
            } else if (uop.action == "mem_read") {
                if (!has_arg(uop, "addr") || !has_arg(uop, "out")) return false;
            } else if (uop.action == "mem_write") {
                if (!has_arg(uop, "addr") || !has_arg(uop, "data")) return false;
            } else if (uop.action == "port_read") {
                if (!has_arg(uop, "port") || !has_arg(uop, "out")) return false;
            } else if (uop.action == "port_write") {
                if (!has_arg(uop, "port") || !has_arg(uop, "data")) return false;
            } else if (uop.action == "coproc_read") {
                if (!has_arg(uop, "cp") || !has_arg(uop, "reg") || !has_arg(uop, "out")) return false;
            } else if (uop.action == "coproc_write") {
                if (!has_arg(uop, "cp") || !has_arg(uop, "reg") || !has_arg(uop, "data")) return false;
            } else if (uop.action == "branch") {
                if (!has_arg(uop, "target")) return false;
            } else if (uop.action != "halt") {
                return false;
            }
            for (const auto &[key, value] : uop.args) {
                const bool writable = key == "dest" || key == "out";
                if ((key == "source" || key == "dest" || key == "out" || key == "a" ||
                     key == "b" || key == "c" || key == "addr" || key == "data" ||
                     key == "port" || key == "cp" || key == "reg" || key == "target") &&
                    !valid_operand(value, writable)) return false;
            }
            if (uop.action == "branch" && uop.args.contains("condition")) {
                std::string condition = uop.args.at("condition");
                if (!condition.empty() && condition[0] == '!') condition.erase(0, 1);
                if (condition.empty() || std::none_of(alu_flags.begin(), alu_flags.end(),
                    [&](const FlagDef &flag) { return flag.name == condition || flag.type == condition; }))
                    return false;
            }
        }
    }

    for (size_t i = 0; i < memory_segments.size(); ++i) {
        const auto &segment = memory_segments[i];
        if (segment.start > segment.end || segment.end >= memory_size)
            return false;
        for (size_t k = 0; k < i; ++k)
            if (!(segment.end < memory_segments[k].start || segment.start > memory_segments[k].end))
                return false;
    }
    std::unordered_set<std::string> peripheral_names;
    for (const auto &peripheral : peripherals) {
        if (peripheral.name.empty() || !peripheral_names.insert(peripheral.name).second ||
            peripheral.address_start > peripheral.address_end || peripheral.address_end >= memory_size ||
            (peripheral.type != "text_display" && peripheral.type != "grid_display" &&
             peripheral.type != "input" && peripheral.type != "declarative"))
            return false;
        if (peripheral.type == "grid_display" && peripheral.parameters.contains("width")) {
            try {
                if (std::stoi(peripheral.parameters.at("width")) <= 0) return false;
            } catch (...) { return false; }
        }
        std::unordered_set<std::string> peripheral_reg_names;
        for (const auto &reg : peripheral.registers) {
            if (reg.offset < 0 || reg.size_bytes < 1 || reg.size_bytes > 16 ||
                !peripheral_reg_names.insert(reg.name).second ||
                static_cast<word_t>(reg.offset) + static_cast<word_t>(reg.size_bytes) - 1 >
                    peripheral.address_end - peripheral.address_start ||
                (reg.access != "r" && reg.access != "w" && reg.access != "rw"))
                return false;
        }
    }

    for (size_t i = 0; i < peripherals.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            const auto &a = peripherals[i];
            const auto &b = peripherals[j];
            if (a.address_end < b.address_start || a.address_start > b.address_end)
                continue;
            const bool layered_terminal = (a.type == "text_display" && b.type == "declarative") ||
                                          (a.type == "declarative" && b.type == "text_display");
            if (!layered_terminal) return false;
        }
    }

    return true;
}

std::string Config::get_error() const {
    return validate() ? "" : "Invalid CPU configuration";
}
