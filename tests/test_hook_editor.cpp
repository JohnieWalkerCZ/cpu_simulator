// Tests for the graphical hook editor logic (src/ui/hook_editor.hpp):
// call arities, assignable targets, default nodes, representability,
// and a round-trip of the shipped hook examples through the blocks
// representation. Runs from the tests/ working directory, so configs
// live at ../configs/8bit.json.
#include "ui/hook_editor.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>

using nlohmann::json;
using namespace HookEditor;

static json Load(const std::string &path) {
    std::ifstream f(path);
    assert(f.is_open());
    json j;
    f >> j;
    return j;
}

static const json *FindPeriph(const json &doc, const std::string &name) {
    for (const auto &p : doc["peripherals"])
        if (p.value("name", "") == name)
            return &p;
    return nullptr;
}

static const json *FindPeriphReg(const json &periph,
                                 const std::string &name) {
    if (!periph.contains("registers"))
        return nullptr;
    for (const auto &r : periph["registers"])
        if (r.value("name", "") == name)
            return &r;
    return nullptr;
}

static bool Contains(const std::vector<std::string> &v,
                     const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

int main() {
    // Call arities match the evaluator (declarative_peripheral.cpp).
    assert(CallFuncArity("trigger_interrupt") == 1);
    assert(CallFuncArity("sys_write") == 2);
    assert(CallFuncArity("host_print") == 1);
    assert(CallFuncArity("sys_read") == -1); // expression fn, not a call
    assert(CallFuncArity("nonsense") == -1);
    assert(CallFuncs() ==
           (std::vector<std::string>{"trigger_interrupt", "sys_write",
                                     "host_print"}));

    // Default nodes per type.
    {
        const std::vector<std::string> targets = {"RESULT", "COUNT"};
        json n = DefaultNode("assign", targets);
        assert(n["type"] == "assign" && n["target"] == "RESULT" &&
               n["expr"] == "0");
        // No targets: empty target, still a valid assign shape.
        n = DefaultNode("assign");
        assert(n["type"] == "assign" && n["target"] == "" &&
               n["expr"] == "0");
        n = DefaultNode("if", targets);
        assert(n["type"] == "if" && n["condition"] == "1" &&
               n["then"].is_array() && n["then"].empty() &&
               !n.contains("else"));
        n = DefaultNode("call", targets);
        assert(n["type"] == "call" &&
               n["func"] == "trigger_interrupt" &&
               n["args"] == json::array({"0"}));
        // Unknown type defaults to assign.
        n = DefaultNode("while", targets);
        assert(n["type"] == "assign");
    }

    // Assignable targets: register names first, then internal-state
    // keys, in document order.
    {
        json periph = json::parse(
            R"({"registers": [{"name": "OP_A"}, {"name": "CMD"}],
                "internal_state": {"COUNT": 0, "PENDING": 0}})");
        assert(AvailableTargets(periph) ==
               (std::vector<std::string>{"OP_A", "CMD", "COUNT",
                                         "PENDING"}));
        json bare = json::parse(R"({"name": "P"})");
        assert(AvailableTargets(bare).empty());
    }

    // Representability: the three node types with their required
    // fields, and everything else rejected.
    {
        assert(IsRepresentable(json::parse(
            R"({"type": "assign", "target": "X", "expr": "X + 1"})")));
        assert(IsRepresentable(json::parse(
            R"({"type": "if", "condition": "value == 1", "then": []})")));
        assert(IsRepresentable(json::parse(
            R"({"type": "if", "condition": "value == 1", "then": [],
                "else": [{"type": "call", "func": "host_print",
                          "args": ["value"]}]})")));
        assert(IsRepresentable(json::parse(
            R"({"type": "call", "func": "sys_write",
                "args": ["0x100", "value"]})")));
        assert(IsRepresentable(json::parse(
            R"({"type": "call", "func": "host_print"})"))); // args optional
        assert(!IsRepresentable(json::parse(R"({"target": "X"})")));
        assert(!IsRepresentable(json::parse(R"({"type": "while"})")));
        assert(!IsRepresentable(json::parse(
            R"({"type": "assign", "target": "X"})"))); // missing expr
        assert(!IsRepresentable(json::parse(
            R"({"type": "assign", "target": 5, "expr": "0"})")));
        assert(!IsRepresentable(json::parse(
            R"({"type": "if", "condition": "value", "then": {}})")));
        assert(!IsRepresentable(json::parse(
            R"({"type": "if", "condition": "value"})"))); // missing then
        assert(!IsRepresentable(json::parse(
            R"({"type": "if", "condition": "value", "then": [],
                "else": {}})")));
        assert(!IsRepresentable(json::parse(
            R"({"type": "if", "condition": "value",
                "then": [{"type": "while"}]})"))); // bad child
        assert(!IsRepresentable(json::parse(
            R"({"type": "call", "func": "host_print", "args": [1]})")));
        assert(!IsRepresentable(json::parse(R"("assign")")));
        // A node with an extra unknown field is still representable
        // (the UI edits its fields in place and never drops it).
        assert(IsRepresentable(json::parse(
            R"({"type": "assign", "target": "X", "expr": "0",
                "comment": "hi"})")));
        // ...but the canonical form drops the extra field.
        assert(CanonicalNode(json::parse(
                   R"({"type": "assign", "target": "X", "expr": "0",
                       "comment": "hi"})")) ==
               json::parse(
                   R"({"type": "assign", "target": "X", "expr": "0"})"));
        // Unrepresentable nodes canonicalize to themselves.
        json weird = json::parse(R"({"type": "while"})");
        assert(CanonicalNode(weird) == weird);
        assert(CanonicalHook(json::parse(R"({})")).empty());
    }

    // Round-trip of the shipped examples: every node is representable
    // and rebuilding it from its own fields yields identical JSON, so
    // the blocks editor neither loses nor alters anything.
    json doc = Load("../configs/8bit.json");
    {
        const json *timer = FindPeriph(doc, "SystemTimer");
        assert(timer != nullptr);
        const json &tick = (*timer)["tick_behavior"];
        assert(tick ==
               json::parse(
                   R"([{"type": "assign", "target": "TICKS",
                       "expr": "TICKS + 1"}])"));
        for (const auto &node : tick)
            assert(IsRepresentable(node));
        assert(CanonicalHook(tick) == tick);
    }
    {
        const json *math = FindPeriph(doc, "MathCoProcessor");
        assert(math != nullptr);
        const json *cmd = FindPeriphReg(*math, "CMD");
        assert(cmd != nullptr);
        const json &on_write = (*cmd)["on_write"];
        assert(on_write.size() == 2);
        for (const auto &node : on_write)
            assert(IsRepresentable(node));
        assert(CanonicalHook(on_write) == on_write);
        assert(AvailableTargets(*math) ==
               (std::vector<std::string>{"OP_A", "OP_B", "RESULT",
                                         "CMD"}));
    }
    {
        // DeclarativeUART TX on_write: a single host_print call.
        const json *uart = FindPeriph(doc, "DeclarativeUART");
        assert(uart != nullptr);
        const json *tx = FindPeriphReg(*uart, "UART_TX");
        assert(tx != nullptr);
        const json &on_write = (*tx)["on_write"];
        assert(on_write.size() == 1);
        assert(IsRepresentable(on_write[0]));
        assert(CanonicalHook(on_write) == on_write);
    }

    std::cout << "All hook editor tests passed!" << std::endl;
    return 0;
}
