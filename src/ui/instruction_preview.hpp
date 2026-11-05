#pragma once
// Pure (no ImGui) derivation of an instruction preview from the JSON
// configuration document. The configurator renders the result; the logic
// lives here so it can be tested without a GUI.
//
// What it computes, for one instruction:
//   - assembly form: NAME + encoding operands (e.g. "ADD dest, src")
//   - per micro-op: inputs -> outputs, and the cycles it takes
//     (an "alu" micro-op takes its ALU operation's latency, every other
//     micro-op takes 1 cycle -- the same rule the executor uses)
//   - effective latency: the sum of the micro-op latencies combined with
//     the instruction latency according to the *effective* latency mode
//     (see EffectiveLatencyMode: this mirrors what Config::from_json
//     actually does, including forcing dynamic when no latency is set
//     and mapping an unknown/dynamic mode string to bottleneck when a
//     latency is set)
//   - warnings for unresolved references: @name not among the encoding
//     operands, $REG not a register in the configuration, an ALU op name
//     not defined in alu.operations, and missing required micro-op args.

#include <algorithm>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace InstructionPreview {

inline std::string ScalarToString(const nlohmann::json &v) {
    if (v.is_string())
        return v.get<std::string>();
    return v.dump();
}

// Argument value of a micro-op as a plain string ("" when absent).
inline std::string Arg(const nlohmann::json &uop, const char *key) {
    if (!uop.is_object() || !uop.contains(key))
        return "";
    return ScalarToString(uop[key]);
}

inline bool HasArg(const nlohmann::json &uop, const char *key) {
    return uop.is_object() && uop.contains(key);
}

// All register names in the document, including recursive sub-registers.
inline std::unordered_set<std::string> CollectRegisterNames(
    const nlohmann::json &doc) {
    std::unordered_set<std::string> names;
    std::function<void(const nlohmann::json &)> walk =
        [&](const nlohmann::json &reg) {
            if (!reg.is_object())
                return;
            const std::string name = reg.value("name", "");
            if (!name.empty())
                names.insert(name);
            if (reg.contains("sub_registers"))
                for (const auto &sub : reg["sub_registers"])
                    walk(sub);
        };
    if (doc.contains("registers") && doc["registers"].is_object()) {
        for (const char *group : {"general_purpose", "special"}) {
            if (!doc["registers"].contains(group))
                continue;
            for (const auto &reg : doc["registers"][group])
                walk(reg);
        }
    }
    return names;
}

// Operand names of an instruction: the string tokens of its encoding
// (numeric tokens are literal fields, e.g. the opcode).
inline std::unordered_set<std::string> EncodingOperandNames(
    const nlohmann::json &inst) {
    std::unordered_set<std::string> names;
    if (!inst.contains("encoding") || !inst["encoding"].is_array())
        return names;
    for (const auto &tok : inst["encoding"])
        if (tok.is_string())
            names.insert(tok.get<std::string>());
    return names;
}

// Latency of a named ALU operation; returns false when it is not defined.
inline bool AluOpLatency(const nlohmann::json &doc, const std::string &name,
                         int &latency) {
    latency = 1;
    if (!doc.contains("alu") || !doc["alu"].is_object() ||
        !doc["alu"].contains("operations"))
        return false;
    for (const auto &op : doc["alu"]["operations"]) {
        if (op.value("name", "") == name) {
            latency = op.value("latency", 1);
            return true;
        }
    }
    return false;
}

// The mode the loader actually applies, mirroring Config::from_json:
// a latency is only honoured when it is > 0; otherwise the mode is
// forced to dynamic. With a latency set, "strict"/"fixed" means strict,
// "bottleneck"/"max" means bottleneck, "additive" means additive, and
// anything else (including "dynamic" and unknown strings) means
// bottleneck.
inline std::string EffectiveLatencyMode(const std::string &raw, int latency) {
    if (latency <= 0)
        return "dynamic";
    if (raw == "strict" || raw == "fixed")
        return "strict";
    if (raw == "bottleneck" || raw == "max")
        return "bottleneck";
    if (raw == "additive")
        return "additive";
    return "bottleneck";
}

inline std::string ModeExplanation(const std::string &effective_mode) {
    if (effective_mode == "strict")
        return "Strict: exactly the instruction latency in cycles. "
               "Micro-ops are compressed into fewer cycles or padded to "
               "fill the window.";
    if (effective_mode == "bottleneck")
        return "Bottleneck: the slower of the instruction latency and the "
               "micro-op latencies wins.";
    if (effective_mode == "additive")
        return "Additive: the micro-ops run first, then the instruction "
               "latency is added as extra cycles on top.";
    return "Dynamic: one micro-op per cycle; an ALU micro-op stalls for "
           "its operation's latency. The instruction latency is ignored.";
}

struct UopPreview {
    std::string action;
    std::string summary; // human-readable, e.g. "ADD(@dest, @src) -> @dest"
    std::vector<std::string> inputs;  // "a = @dest"
    std::vector<std::string> outputs; // "@dest", "FLAGS", "PC", "memory[@x]"
    int latency = 1;
};

struct Preview {
    std::string name;
    std::string assembly; // e.g. "ADD dest, src"
    std::vector<std::string> encoding_tokens; // every encoding token
    std::vector<UopPreview> uops;
    int uop_latency_sum = 0;
    int latency = -1; // instruction latency, -1 = none
    std::string mode_raw;       // as written in the JSON
    std::string mode_effective; // as the loader applies it
    int effective_cycles = 0;
    std::string effective_explanation; // one line, e.g. "max(3, 2) = 3 cycles"
    std::vector<std::string> warnings;
};

inline Preview Build(const nlohmann::json &doc, const nlohmann::json &inst) {
    Preview pv;
    pv.name = inst.value("name", "?");
    pv.latency = inst.value("latency", -1);
    pv.mode_raw = inst.value("latency_mode", "dynamic");
    pv.mode_effective = EffectiveLatencyMode(pv.mode_raw, pv.latency);

    if (inst.contains("encoding") && inst["encoding"].is_array())
        for (const auto &tok : inst["encoding"])
            pv.encoding_tokens.push_back(ScalarToString(tok));

    // Assembly form: NAME + encoding operands, skipping a leading numeric
    // opcode token (e.g. [5, "dest", "src"] -> "ADD dest, src").
    pv.assembly = pv.name;
    {
        std::vector<std::string> operands;
        if (inst.contains("encoding") && inst["encoding"].is_array()) {
            bool first = true;
            for (const auto &tok : inst["encoding"]) {
                if (first && tok.is_number()) {
                    first = false;
                    continue;
                }
                first = false;
                operands.push_back(ScalarToString(tok));
            }
        }
        for (const auto &op : operands) {
            pv.assembly += (pv.assembly == pv.name) ? " " : ", ";
            pv.assembly += op;
        }
    }

    const std::unordered_set<std::string> reg_names =
        CollectRegisterNames(doc);
    const std::unordered_set<std::string> enc_names =
        EncodingOperandNames(inst);
    static const std::unordered_set<std::string> kSpecialRegs = {
        "PC", "SP", "FLAGS", "NEXT_PC"};

    if (pv.latency <= 0)
        pv.warnings.push_back(
            "No instruction latency is set, so the latency mode is "
            "ignored and the loader forces dynamic mode.");
    else if (pv.mode_raw != pv.mode_effective &&
             pv.mode_raw != "fixed" && pv.mode_raw != "max")
        pv.warnings.push_back(
            "The loader maps latency mode '" + pv.mode_raw +
            "' to '" + pv.mode_effective +
            "' when a latency is set (only strict/fixed, "
            "bottleneck/max and additive are honoured as written).");

    if (!inst.contains("microcode") || !inst["microcode"].is_array() ||
        inst["microcode"].empty())
        pv.warnings.push_back("This instruction has no microcode.");

    if (inst.contains("microcode") && inst["microcode"].is_array()) {
        int idx = 0;
        for (const auto &uop : inst["microcode"]) {
            ++idx;
            UopPreview up;
            up.action = uop.value("action", "?");
            const std::string where =
                "Micro-op " + std::to_string(idx) + " (" + up.action + "): ";

            // Unresolved-reference checks over every argument value.
            if (uop.is_object()) {
                for (auto it = uop.begin(); it != uop.end(); ++it) {
                    if (it.key() == "action" || !it.value().is_string())
                        continue;
                    const std::string val = it.value().get<std::string>();
                    if (val.size() > 1 && val[0] == '@') {
                        const std::string token = val.substr(1);
                        if (!enc_names.count(token))
                            pv.warnings.push_back(
                                where + "'" + val + "' in argument '" +
                                it.key() +
                                "' is not an encoding operand of this "
                                "instruction.");
                    } else if (val.size() > 1 && val[0] == '$') {
                        const std::string reg = val.substr(1);
                        if (!kSpecialRegs.count(reg) &&
                            !reg_names.count(reg))
                            pv.warnings.push_back(
                                where + "'" + val + "' in argument '" +
                                it.key() +
                                "' is not a register in this "
                                "configuration.");
                    }
                }
            }

            auto need = [&](const char *key) {
                if (!HasArg(uop, key))
                    pv.warnings.push_back(where + "is missing required "
                                                   "argument '" +
                                          key + "'.");
            };

            if (up.action == "copy") {
                need("source");
                need("dest");
                up.inputs = {"source = " + Arg(uop, "source")};
                up.outputs = {Arg(uop, "dest")};
                up.summary = Arg(uop, "source") + " -> " + Arg(uop, "dest");
            } else if (up.action == "alu") {
                need("a");
                need("op");
                need("out");
                const std::string op_name = Arg(uop, "op");
                int op_latency = 1;
                if (!op_name.empty() &&
                    !AluOpLatency(doc, op_name, op_latency)) {
                    pv.warnings.push_back(
                        where + "ALU operation '" + op_name +
                        "' is not defined in alu.operations.");
                }
                up.latency = op_latency;
                up.inputs = {"a = " + Arg(uop, "a")};
                std::string call = op_name + "(" + Arg(uop, "a");
                if (HasArg(uop, "b")) {
                    up.inputs.push_back("b = " + Arg(uop, "b"));
                    call += ", " + Arg(uop, "b");
                }
                if (HasArg(uop, "c")) {
                    up.inputs.push_back("c = " + Arg(uop, "c"));
                    call += ", " + Arg(uop, "c");
                }
                call += ")";
                up.outputs = {Arg(uop, "out")};
                up.summary = call + " -> " + Arg(uop, "out");
                if (Arg(uop, "update_flags") == "true") {
                    up.outputs.push_back("FLAGS");
                    up.summary += "  [updates FLAGS]";
                }
            } else if (up.action == "mem_read") {
                need("addr");
                need("out");
                up.inputs = {"addr = " + Arg(uop, "addr")};
                up.outputs = {Arg(uop, "out")};
                up.summary =
                    "mem[" + Arg(uop, "addr") + "] -> " + Arg(uop, "out");
            } else if (up.action == "mem_write") {
                need("addr");
                need("data");
                up.inputs = {"addr = " + Arg(uop, "addr"),
                             "data = " + Arg(uop, "data")};
                up.outputs = {"memory[" + Arg(uop, "addr") + "]"};
                up.summary = "mem[" + Arg(uop, "addr") +
                             "] <- " + Arg(uop, "data");
            } else if (up.action == "port_read") {
                need("port");
                need("out");
                up.inputs = {"port = " + Arg(uop, "port")};
                up.outputs = {Arg(uop, "out")};
                up.summary = "port[" + Arg(uop, "port") +
                             "] -> " + Arg(uop, "out");
            } else if (up.action == "port_write") {
                need("port");
                need("data");
                up.inputs = {"port = " + Arg(uop, "port"),
                             "data = " + Arg(uop, "data")};
                up.outputs = {"port[" + Arg(uop, "port") + "]"};
                up.summary = "port[" + Arg(uop, "port") +
                             "] <- " + Arg(uop, "data");
            } else if (up.action == "coproc_read") {
                need("cp");
                need("reg");
                need("out");
                up.inputs = {"cp = " + Arg(uop, "cp"),
                             "reg = " + Arg(uop, "reg")};
                up.outputs = {Arg(uop, "out")};
                up.summary = "coproc[" + Arg(uop, "cp") + ", " +
                             Arg(uop, "reg") + "] -> " + Arg(uop, "out");
            } else if (up.action == "coproc_write") {
                need("cp");
                need("reg");
                need("data");
                up.inputs = {"cp = " + Arg(uop, "cp"),
                             "reg = " + Arg(uop, "reg"),
                             "data = " + Arg(uop, "data")};
                up.outputs = {"coproc[" + Arg(uop, "cp") + ", " +
                              Arg(uop, "reg") + "]"};
                up.summary = "coproc[" + Arg(uop, "cp") + ", " +
                             Arg(uop, "reg") +
                             "] <- " + Arg(uop, "data");
            } else if (up.action == "branch") {
                need("target");
                if (HasArg(uop, "condition"))
                    up.inputs.push_back("condition = " +
                                        Arg(uop, "condition"));
                up.inputs.push_back("target = " + Arg(uop, "target"));
                up.outputs = {"PC"};
                if (HasArg(uop, "condition"))
                    up.summary = "if " + Arg(uop, "condition") + " then PC = " +
                                 Arg(uop, "target");
                else
                    up.summary = "PC = " + Arg(uop, "target");
                if (Arg(uop, "relative") == "true")
                    up.summary += "  (relative)";
            } else if (up.action == "halt") {
                up.summary = "halt: stop the CPU";
            } else {
                up.summary = "unknown action '" + up.action + "'";
                pv.warnings.push_back(where + "has an unknown action '" +
                                      up.action + "'.");
            }
            pv.uop_latency_sum += up.latency;
            pv.uops.push_back(std::move(up));
        }
    }

    const int sum = pv.uop_latency_sum;
    const int lat = pv.latency;
    if (pv.mode_effective == "strict") {
        pv.effective_cycles = lat;
        pv.effective_explanation = "strict: exactly " +
                                   std::to_string(lat) + " cycles";
    } else if (pv.mode_effective == "bottleneck") {
        pv.effective_cycles = std::max(lat, sum);
        pv.effective_explanation = "bottleneck: max(" +
                                   std::to_string(lat) + ", " +
                                   std::to_string(sum) + ") = " +
                                   std::to_string(pv.effective_cycles) +
                                   " cycles";
    } else if (pv.mode_effective == "additive") {
        pv.effective_cycles = sum + lat;
        pv.effective_explanation =
            "additive: " + std::to_string(sum) + " (micro-ops) + " +
            std::to_string(lat) + " (latency) = " +
            std::to_string(pv.effective_cycles) + " cycles";
    } else {
        pv.effective_cycles = sum;
        pv.effective_explanation =
            "dynamic: " + std::to_string(sum) +
            " cycles of micro-ops (instruction latency ignored)";
    }
    return pv;
}

} // namespace InstructionPreview
