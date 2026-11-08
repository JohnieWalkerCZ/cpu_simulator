#pragma once
// Pure (no ImGui) logic for the graphical hook editor in the
// configurator. Peripheral hooks (on_read / on_write on a peripheral
// register, tick_behavior on the peripheral itself) are JSON arrays of
// AST nodes, and the old editor was one pretty-printed raw JSON box, so
// a typo only surfaced as a loader error. The new editor instead shows
// the AST as a list of node blocks, each with exactly the fields its
// node type needs.
//
// The AST format (see CONFIGURATION.md 6.3 and
// DeclarativePeripheral::execute_ast) has only three node types:
//   - if: "condition" (string expression), "then" (array of nodes),
//     "else" (array, optional).
//   - assign: "target" (a peripheral register name or an internal-state
//     key), "expr" (string expression).
//   - call: "func" (trigger_interrupt = 1 arg, sys_write = 2 args,
//     host_print = 1 arg), "args" (array of expression strings,
//     optional).
// Expressions are C-like; identifiers are register names, internal
// state names and the context variable `value`; literals are decimal /
// hex / binary; sys_read(addr) and host_pop_char() are usable inside
// expressions.
//
// What lives here (testable without a GUI):
//   - CallFuncArity(func): expected argument count, -1 when unknown.
//   - AvailableTargets(periph): assignable targets -- the peripheral's
//     register names, then its internal-state keys.
//   - DefaultNode(type, targets): a fresh node of the given type; a new
//     node defaults to assign with the first valid target and "0".
//   - IsRepresentable(node): whether the blocks editor can show this
//     node losslessly (known type, required fields present with the
//     right JSON types, children representable too).
//   - CanonicalNode(node) / CanonicalHook(hook): rebuild a node (or a
//     whole hook) from its own known fields only. CanonicalNode(node)
//     == node exactly when the blocks editor round-trips the node
//     without loss; a node with unknown extra fields is still
//     representable, but the UI edits its fields in place and never
//     rebuilds it, so nothing is dropped.
// The UI adds a "Custom..." entry to the target/func pickers that falls
// back to a free-text field, so nothing expressible in JSON becomes
// impossible; the raw JSON tab and the per-hook Text view remain the
// escape hatches for anything else.

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace HookEditor {

// The three node types the evaluator understands, in form order.
inline std::vector<std::string> NodeTypes() {
    return {"if", "assign", "call"};
}

// Expected argument count of a built-in call function, -1 when the
// function is not one of the three the evaluator handles.
inline int CallFuncArity(const std::string &func) {
    if (func == "trigger_interrupt")
        return 1;
    if (func == "sys_write")
        return 2;
    if (func == "host_print")
        return 1;
    return -1;
}

inline std::vector<std::string> CallFuncs() {
    return {"trigger_interrupt", "sys_write", "host_print"};
}

// Assignable targets for a peripheral: its register names first, then
// its internal-state keys.
inline std::vector<std::string> AvailableTargets(
    const nlohmann::json &periph) {
    std::vector<std::string> out;
    if (periph.contains("registers") && periph["registers"].is_array())
        for (const auto &r : periph["registers"])
            if (r.is_object() && r.contains("name") &&
                r["name"].is_string())
                out.push_back(r["name"].get<std::string>());
    if (periph.contains("internal_state") &&
        periph["internal_state"].is_object())
        for (auto it = periph["internal_state"].begin();
             it != periph["internal_state"].end(); ++it)
            out.push_back(it.key());
    return out;
}

// A fresh node of the given type. assign takes the first valid target
// ("" when the peripheral has none) and expr "0"; if starts with
// condition "1" and an empty then list (no else); call starts as
// trigger_interrupt with one argument "0". An unknown type defaults to
// assign.
inline nlohmann::json DefaultNode(
    const std::string &type,
    const std::vector<std::string> &targets = {}) {
    if (type == "if")
        return {{"type", "if"},
                {"condition", "1"},
                {"then", nlohmann::json::array()}};
    if (type == "call")
        return {{"type", "call"},
                {"func", "trigger_interrupt"},
                {"args", nlohmann::json::array({"0"})}};
    return {{"type", "assign"},
            {"target", targets.empty() ? std::string("") : targets[0]},
            {"expr", "0"}};
}

// Whether the blocks editor can show this node losslessly: a known
// type, the required fields present with the right JSON types, and
// (for if) every child node representable too. A node with an unknown
// type, a missing field, or a wrong field type is shown greyed-out
// instead, preserved as-is.
inline bool IsRepresentable(const nlohmann::json &node) {
    if (!node.is_object() || !node.contains("type") ||
        !node["type"].is_string())
        return false;
    const std::string type = node["type"].get<std::string>();
    if (type == "if") {
        if (!node.contains("condition") || !node["condition"].is_string())
            return false;
        if (!node.contains("then") || !node["then"].is_array())
            return false;
        if (node.contains("else") && !node["else"].is_array())
            return false;
        for (const auto &child : node["then"])
            if (!IsRepresentable(child))
                return false;
        if (node.contains("else"))
            for (const auto &child : node["else"])
                if (!IsRepresentable(child))
                    return false;
        return true;
    }
    if (type == "assign")
        return node.contains("target") && node["target"].is_string() &&
               node.contains("expr") && node["expr"].is_string();
    if (type == "call") {
        if (!node.contains("func") || !node["func"].is_string())
            return false;
        if (!node.contains("args"))
            return true;
        if (!node["args"].is_array())
            return false;
        for (const auto &arg : node["args"])
            if (!arg.is_string())
                return false;
        return true;
    }
    return false;
}

// Rebuild a representable node from its own known fields only. For an
// unrepresentable node the input is returned unchanged.
inline nlohmann::json CanonicalNode(const nlohmann::json &node) {
    if (!IsRepresentable(node))
        return node;
    const std::string type = node["type"].get<std::string>();
    if (type == "if") {
        nlohmann::json out = {{"type", "if"},
                              {"condition", node["condition"]},
                              {"then", nlohmann::json::array()}};
        for (const auto &child : node["then"])
            out["then"].push_back(CanonicalNode(child));
        if (node.contains("else")) {
            out["else"] = nlohmann::json::array();
            for (const auto &child : node["else"])
                out["else"].push_back(CanonicalNode(child));
        }
        return out;
    }
    if (type == "assign")
        return {{"type", "assign"},
                {"target", node["target"]},
                {"expr", node["expr"]}};
    nlohmann::json out = {{"type", "call"}, {"func", node["func"]}};
    if (node.contains("args"))
        out["args"] = node["args"];
    return out;
}

// CanonicalNode applied to every node of a hook array; a non-array
// hook canonicalizes to an empty array.
inline nlohmann::json CanonicalHook(const nlohmann::json &hook) {
    nlohmann::json out = nlohmann::json::array();
    if (hook.is_array())
        for (const auto &node : hook)
            out.push_back(CanonicalNode(node));
    return out;
}

} // namespace HookEditor
