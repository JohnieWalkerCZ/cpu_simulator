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

// ---------------------------------------------------------------------------
// Node paths and drag-and-drop moves (the Scratch-style editor).
//
// A path addresses a node or a node list inside a hook array: a list of
// segments, where a numeric segment is an index into a node list and a
// "then"/"else" segment descends into an if node's child list. A path
// ending in an index addresses a node ("1/then/0"); a path ending in a
// key (or the empty path) addresses a list ("1/then", ""). Paths are
// what the drag-and-drop payloads carry, so the move model lives here
// and is unit-testable without a GUI.

struct PathSeg {
    bool is_index = true;
    int index = 0;       // valid when is_index
    std::string key;     // "then" or "else" when !is_index
    static PathSeg Index(int i) {
        PathSeg s;
        s.is_index = true;
        s.index = i;
        return s;
    }
    static PathSeg Key(const std::string &k) {
        PathSeg s;
        s.is_index = false;
        s.key = k;
        return s;
    }
    bool operator==(const PathSeg &o) const {
        return is_index == o.is_index && index == o.index && key == o.key;
    }
};

using NodePath = std::vector<PathSeg>;

// "1/then/0", "0", "" (the empty path is the hook's top-level list).
inline std::string PathToString(const NodePath &path) {
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0)
            out += "/";
        out += path[i].is_index ? std::to_string(path[i].index)
                                : path[i].key;
    }
    return out;
}

// Inverse of PathToString; false (and `path` untouched) on any segment
// that is neither a non-negative integer nor "then"/"else".
inline bool ParsePath(const std::string &text, NodePath &path) {
    NodePath parsed;
    if (text.empty()) {
        path.clear(); // the empty path is the top-level list
        return true;
    }
    size_t start = 0;
    while (true) {
        const size_t slash = text.find('/', start);
        const std::string seg = text.substr(
            start, slash == std::string::npos ? std::string::npos
                                              : slash - start);
        if (seg == "then" || seg == "else") {
            parsed.push_back(PathSeg::Key(seg));
        } else if (!seg.empty() &&
                   std::all_of(seg.begin(), seg.end(), [](char c) {
                       return c >= '0' && c <= '9';
                   })) {
            parsed.push_back(PathSeg::Index(std::stoi(seg)));
        } else {
            return false;
        }
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    path = parsed;
    return true;
}

// The parent path (everything but the last segment); the empty path's
// parent is itself.
inline NodePath ParentPath(const NodePath &path) {
    if (path.empty())
        return path;
    return NodePath(path.begin(), path.end() - 1);
}

// Whether `prefix` is a prefix of `path` (a path is a prefix of
// itself).
inline bool IsPrefix(const NodePath &prefix, const NodePath &path) {
    if (prefix.size() > path.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (!(prefix[i] == path[i]))
            return false;
    return true;
}

namespace detail {

template <typename Json>
Json *FindNodeImpl(Json &hook, const NodePath &path) {
    // Walk alternating list -> node -> child list; `node` is the node
    // addressed so far (null while standing on a list).
    Json *list = &hook;
    Json *node = nullptr;
    for (const auto &seg : path) {
        if (seg.is_index) {
            if (list == nullptr || !list->is_array() || seg.index < 0 ||
                seg.index >= static_cast<int>(list->size()))
                return nullptr;
            node = &(*list)[static_cast<size_t>(seg.index)];
        } else {
            if (node == nullptr || !node->is_object() ||
                !node->contains(seg.key) || !(*node)[seg.key].is_array())
                return nullptr;
            list = &(*node)[seg.key];
            node = nullptr;
        }
    }
    return node; // null when the path addresses a list (or is empty)
}

template <typename Json>
Json *FindListImpl(Json &hook, const NodePath &list_path) {
    if (list_path.empty())
        return hook.is_array() ? &hook : nullptr;
    if (list_path.back().is_index)
        return nullptr; // a list path ends in a key, never an index
    Json *node = FindNodeImpl(hook, ParentPath(list_path));
    if (node == nullptr)
        return nullptr;
    Json &arr = (*node)[list_path.back().key];
    return arr.is_array() ? &arr : nullptr;
}

} // namespace detail

inline const nlohmann::json *FindNode(const nlohmann::json &hook,
                                      const NodePath &path) {
    return detail::FindNodeImpl(hook, path);
}

inline nlohmann::json *FindNode(nlohmann::json &hook,
                                const NodePath &path) {
    return detail::FindNodeImpl(hook, path);
}

// The node list addressed by `list_path` (the empty path is the hook
// itself); null when the path does not address a list.
inline const nlohmann::json *FindList(const nlohmann::json &hook,
                                      const NodePath &list_path) {
    return detail::FindListImpl(hook, list_path);
}

inline nlohmann::json *FindList(nlohmann::json &hook,
                                const NodePath &list_path) {
    return detail::FindListImpl(hook, list_path);
}

// Whether moving the node at `from` into the list at `to_list` would
// put the node inside itself (or into one of its own child lists).
// Such a drop is refused by MoveNode and must not be offered by the
// editor either.
inline bool MoveIsSelfTarget(const NodePath &from,
                             const NodePath &to_list) {
    NodePath owner = to_list;
    if (!owner.empty() && !owner.back().is_index)
        owner.pop_back();
    return IsPrefix(from, owner);
}

// Move the node at `from` to position `to_index` in the list at
// `to_list` (indices clamp to the list, so an index past the end
// appends). `to_index` is a position in the list as the user sees it
// before the move -- dropping a block after the item at index i passes
// i+1 -- so moving within one list adjusts for the removal. Returns
// false (hook untouched) for an invalid source or target, or for a
// move into the node's own descendant.
inline bool MoveNode(nlohmann::json &hook, const NodePath &from,
                     const NodePath &to_list, int to_index) {
    if (from.empty() || !from.back().is_index)
        return false;
    if (MoveIsSelfTarget(from, to_list))
        return false;
    nlohmann::json *src_list = FindList(hook, ParentPath(from));
    nlohmann::json *dst_list = FindList(hook, to_list);
    if (src_list == nullptr || dst_list == nullptr)
        return false;
    const int from_index = from.back().index;
    if (from_index < 0 ||
        from_index >= static_cast<int>(src_list->size()))
        return false;
    nlohmann::json node = (*src_list)[static_cast<size_t>(from_index)];
    src_list->erase(src_list->begin() + from_index);
    if (src_list == dst_list && to_index > from_index)
        --to_index;
    to_index =
        std::clamp(to_index, 0, static_cast<int>(dst_list->size()));
    dst_list->insert(dst_list->begin() + to_index, node);
    return true;
}

// Insert a copy of `node` at `to_index` in the list at `to_list`
// (clamped, so past the end appends). False when the list path is
// invalid.
inline bool InsertNode(nlohmann::json &hook, const NodePath &to_list,
                       int to_index, const nlohmann::json &node) {
    nlohmann::json *dst_list = FindList(hook, to_list);
    if (dst_list == nullptr)
        return false;
    to_index =
        std::clamp(to_index, 0, static_cast<int>(dst_list->size()));
    dst_list->insert(dst_list->begin() + to_index, node);
    return true;
}

// Erase the node at `path`; false (hook untouched) for an invalid
// path.
inline bool RemoveNode(nlohmann::json &hook, const NodePath &path) {
    if (path.empty() || !path.back().is_index)
        return false;
    nlohmann::json *list = FindList(hook, ParentPath(path));
    if (list == nullptr)
        return false;
    const int idx = path.back().index;
    if (idx < 0 || idx >= static_cast<int>(list->size()))
        return false;
    list->erase(list->begin() + idx);
    return true;
}

} // namespace HookEditor
