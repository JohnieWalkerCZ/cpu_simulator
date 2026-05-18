#include "config.hpp"
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

Config Config::from_json(const nlohmann::json &j) {
    Config cfg;

    cfg.name = j.at("name").get<std::string>();
    cfg.data_width = j["data_bus"].at("width").get<int>();
    cfg.addr_width = j["address_bus"].at("width").get<int>();
    cfg.memory_size = j["memory"].at("size").get<int>();

    if (j.contains("registers")) {
        const auto &regs = j["registers"];
        if (regs.contains("general_purpose")) {
            for (const auto &r : regs["general_purpose"]) {
                RegisterDef reg;
                reg.name = r.at("name").get<std::string>();
                reg.width = r.at("width").get<int>();
                reg.initial = r.value("initial", 0ULL);
                reg.role = "";
                cfg.registers.push_back(reg);
            }
        }
        if (regs.contains("special")) {
            for (const auto &r : regs["special"]) {
                RegisterDef reg;
                reg.name = r.at("name").get<std::string>();
                reg.width = r.at("width").get<int>();
                reg.initial = r.value("initial", 0ULL);
                reg.role = r.value("role", "");
                cfg.registers.push_back(reg);
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
                flag.type = f.at("type").get<std::string>();
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
            ins.opcode = inst.at("opcode").get<uint8_t>();
            ins.format = inst.value("format", "RR");

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
    std::unordered_set<uint8_t> opcodes;
    std::unordered_set<uint8_t> alu_codes;

    if (data_width < 4 || data_width > 64 ||
        (data_width & (data_width - 1)) != 0) {
        return false;
    }

    if (addr_width < 4 || addr_width > 64 ||
        (addr_width & (addr_width - 1)) != 0) {
        return false;
    }

    uint64_t max_adressable = 1ULL << addr_width;
    if (memory_size > max_adressable) {
        return false;
    }

    for (const RegisterDef &reg : registers) {
        if (names.count(reg.name))
            return false;

        names.insert(reg.name);

        if (reg.width < 1 || reg.width > 64)
            return false;

        uint64_t max_val =
            (reg.width == 64) ? UINT64_MAX : (1ULL << reg.width) - 1;
        if (reg.initial > max_val)
            return false;

        if (!reg.role.empty()) {
            if (reg.role != "pc" && reg.role != "sp" && reg.role != "flags" &&
                reg.role != "ir" && reg.role != "accumulator")
                return false;
        }
    }

    bool has_pc = false;
    for (const RegisterDef &reg : registers) {
        if (reg.role == "pc") {
            has_pc = true;
            break;
        }
    }
    if (!has_pc)
        return false;

    for (const ALUOp &op : alu_ops) {
        if (alu_codes.count(op.code))
            return false;
        alu_codes.insert(op.code);

        if (op.latency < 1)
            return false;
    }

    for (const Instruction ins : instructions) {
        if (opcodes.count(ins.opcode))
            return false;
        opcodes.insert(ins.opcode);
        if (ins.format != "RR" && ins.format != "RI" && ins.format != "R" &&
            ins.format != "R_M" && ins.format != "I")
            return false;
    }

    return true;
}
