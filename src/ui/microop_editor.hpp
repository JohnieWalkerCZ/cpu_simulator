#pragma once
// Pure (no ImGui) logic for the guided micro-op editor in the
// configurator. The old editor was one free-text "key = value" box, so
// a typo such as out = @sr (instead of @src) only surfaced as a
// Problem after the fact. The new editor instead offers, for the picked
// action, exactly the arguments that action needs, each as its own
// field, and operand values come from a picker listing only the values
// that are valid in that slot.
//
// What lives here (testable without a GUI):
//   - ArgSpecs(action): the arguments of one micro-op action, in form
//     order, with the same required/optional split the loader enforces
//     (see Config::validate() and ConfigDiagnostics::Diagnose): copy
//     source+dest; alu op+a+out (+ optional b, c, update_flags);
//     mem_read addr+out; mem_write addr+data; port_read port+out;
//     port_write port+data; coproc_read/write cp+reg+out/data; branch
//     target (+ optional condition, relative); halt none. Micro-op
//     arguments are flat keys on the micro-op object, not an "args"
//     sub-object.
//   - OperandOptions(doc, inst, writable): the valid operand
//     references for one slot: the instruction's encoding operands
//     (@dest, @src, ...) first, then the registers ($PC, $SP, $FLAGS,
//     $NEXT_PC when reading, plus the configured register names).
//     Writable slots list only writable targets (@dest/@src/@addr_reg
//     and $PC/$SP/$FLAGS) -- never $NEXT_PC and never an immediate
//     operand such as @imm8. A literal (#...) is not in the list: the
//     UI offers it as a separate "# (literal...)" entry that seeds a
//     "#0" value the user then edits as free text.
//   - AluOpNames(doc): the ALU operations defined in alu.operations,
//     sorted, for the alu `op` picker.
//   - ConditionOptions(doc): branch conditions from alu.flags -- each
//     flag name and type, plain and negated ("Z", "!Z", "zero",
//     "!zero", ...).
//   - DefaultValue(doc, inst, spec): the first valid value for a
//     spec, "" when there is none; used to prefill a missing required
//     argument when the action changes.
//
// The UI adds a "Custom..." entry to every picker that falls back to a
// free-text field, so nothing expressible in JSON becomes impossible;
// the raw JSON tab remains the escape hatch for anything else.

#include "config_diagnostics.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <vector>

namespace MicroOpEditor {

enum class ArgKind { kOperand, kAluOp, kCondition, kBool };

struct ArgSpec {
    std::string key;
    std::string label; // shown in the form; optional args say so
    bool required;
    ArgKind kind;
    bool writable; // only meaningful for kOperand
};

inline std::vector<ArgSpec> ArgSpecs(const std::string &action) {
    using K = ArgKind;
    if (action == "copy")
        return {{"source", "source", true, K::kOperand, false},
                {"dest", "dest", true, K::kOperand, true}};
    if (action == "alu")
        return {{"op", "op (ALU operation)", true, K::kAluOp, false},
                {"a", "a", true, K::kOperand, false},
                {"b", "b (optional)", false, K::kOperand, false},
                {"c", "c (optional)", false, K::kOperand, false},
                {"out", "out", true, K::kOperand, true},
                {"update_flags", "update_flags (optional)", false, K::kBool,
                 false}};
    if (action == "mem_read")
        return {{"addr", "addr", true, K::kOperand, false},
                {"out", "out", true, K::kOperand, true}};
    if (action == "mem_write")
        return {{"addr", "addr", true, K::kOperand, false},
                {"data", "data", true, K::kOperand, false}};
    if (action == "port_read")
        return {{"port", "port", true, K::kOperand, false},
                {"out", "out", true, K::kOperand, true}};
    if (action == "port_write")
        return {{"port", "port", true, K::kOperand, false},
                {"data", "data", true, K::kOperand, false}};
    if (action == "coproc_read")
        return {{"cp", "cp", true, K::kOperand, false},
                {"reg", "reg", true, K::kOperand, false},
                {"out", "out", true, K::kOperand, true}};
    if (action == "coproc_write")
        return {{"cp", "cp", true, K::kOperand, false},
                {"reg", "reg", true, K::kOperand, false},
                {"data", "data", true, K::kOperand, false}};
    if (action == "branch")
        return {{"target", "target", true, K::kOperand, false},
                {"condition", "condition (optional)", false, K::kCondition,
                 false},
                {"relative", "relative (optional)", false, K::kBool, false}};
    return {}; // "halt" and unknown actions take no arguments
}

// Canonical encoding operand names of an instruction, in encoding
// order ("dest", "src", "imm8", ...). The "imm" alias maps to "imm8",
// as the loader does, so @imm is not a valid reference.
inline std::vector<std::string> EncodingOperands(
    const nlohmann::json &inst) {
    std::vector<std::string> out;
    if (!inst.contains("encoding") || !inst["encoding"].is_array())
        return out;
    for (const auto &tok : inst["encoding"]) {
        if (!tok.is_string())
            continue;
        const int code =
            ConfigDiagnostics::EncodingTokenCode(tok.get<std::string>());
        const std::string name =
            code == -1   ? "dest"
            : code == -2 ? "src"
            : code == -3 ? "addr_reg"
            : code == -4 ? "offset"
            : code == -5 ? "imm8"
            : code == -6 ? "imm16"
            : code == -7 ? "address"
                         : "";
        if (!name.empty() &&
            std::find(out.begin(), out.end(), name) == out.end())
            out.push_back(name);
    }
    return out;
}

// Valid operand references for one argument slot.
inline std::vector<std::string> OperandOptions(const nlohmann::json &doc,
                                               const nlohmann::json &inst,
                                               bool writable) {
    std::vector<std::string> out;
    for (const auto &name : EncodingOperands(inst)) {
        if (writable && name != "dest" && name != "src" &&
            name != "addr_reg")
            continue; // immediates are not writable targets
        out.push_back("@" + name);
    }
    out.push_back("$PC");
    out.push_back("$SP");
    out.push_back("$FLAGS");
    if (!writable)
        out.push_back("$NEXT_PC"); // read-only: never a write target
    // Configured register names (including sub-registers), sorted,
    // skipping the special ones already listed above.
    const ConfigDiagnostics::Context ctx =
        ConfigDiagnostics::BuildContext(doc);
    std::vector<std::string> regs(ctx.reg_names.begin(),
                                  ctx.reg_names.end());
    std::sort(regs.begin(), regs.end());
    for (const auto &name : regs) {
        if (name == "PC" || name == "SP" || name == "FLAGS" ||
            name == "NEXT_PC" || name.empty())
            continue;
        out.push_back("$" + name);
    }
    return out;
}

// ALU operation names defined in alu.operations, sorted.
inline std::vector<std::string> AluOpNames(const nlohmann::json &doc) {
    const ConfigDiagnostics::Context ctx =
        ConfigDiagnostics::BuildContext(doc);
    std::vector<std::string> names(ctx.alu_names.begin(),
                                   ctx.alu_names.end());
    std::sort(names.begin(), names.end());
    return names;
}

// Branch condition options from alu.flags, in document order: each
// flag name and type, plain and negated.
inline std::vector<std::string> ConditionOptions(
    const nlohmann::json &doc) {
    std::vector<std::string> out;
    auto add = [&](const std::string &v) {
        if (!v.empty() &&
            std::find(out.begin(), out.end(), v) == out.end())
            out.push_back(v);
    };
    if (doc.contains("alu") && doc["alu"].is_object() &&
        doc["alu"].contains("flags") && doc["alu"]["flags"].is_array())
        for (const auto &f : doc["alu"]["flags"]) {
            const std::string name =
                ConfigDiagnostics::JsonString(f, "name");
            const std::string type =
                ConfigDiagnostics::JsonString(f, "type");
            add(name);
            if (!name.empty())
                add("!" + name);
            add(type);
            if (!type.empty())
                add("!" + type);
        }
    return out;
}

// Whether the editor shows a free-text box under the picker for this
// argument: exactly when the current value is set but is not one of
// the picker options (a "#..." literal or a custom value). An unset
// optional argument shows "(not set)" and no box at all.
inline bool ShowsTextBox(const nlohmann::json &doc,
                         const nlohmann::json &inst, const ArgSpec &spec,
                         const std::string &cur) {
    if (spec.kind == ArgKind::kBool || cur.empty())
        return false;
    std::vector<std::string> options;
    if (spec.kind == ArgKind::kOperand)
        options = OperandOptions(doc, inst, spec.writable);
    else if (spec.kind == ArgKind::kAluOp)
        options = AluOpNames(doc);
    else
        options = ConditionOptions(doc);
    return std::find(options.begin(), options.end(), cur) ==
           options.end();
}

// First valid value for a spec, "" when there is none.
inline std::string DefaultValue(const nlohmann::json &doc,
                                const nlohmann::json &inst,
                                const ArgSpec &spec) {
    switch (spec.kind) {
    case ArgKind::kOperand: {
        const std::vector<std::string> opts =
            OperandOptions(doc, inst, spec.writable);
        return opts.empty() ? std::string("") : opts.front();
    }
    case ArgKind::kAluOp: {
        const std::vector<std::string> names = AluOpNames(doc);
        return names.empty() ? std::string("") : names.front();
    }
    case ArgKind::kCondition: {
        const std::vector<std::string> opts = ConditionOptions(doc);
        return opts.empty() ? std::string("") : opts.front();
    }
    case ArgKind::kBool:
        return "";
    }
    return "";
}

} // namespace MicroOpEditor
