// Tests for the guided micro-op editor logic
// (src/ui/microop_editor.hpp): argument specs per action, operand
// picker options, ALU operation names and branch condition options.
// Runs from the tests/ working directory, so configs live at
// ../configs/8bit.json.
#include "ui/microop_editor.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>

using nlohmann::json;
using namespace MicroOpEditor;

static json Load(const std::string &path) {
    std::ifstream f(path);
    assert(f.is_open());
    json j;
    f >> j;
    return j;
}

static const json *FindInst(const json &doc, const std::string &name) {
    for (const auto &i : doc["instruction_set"]["instructions"])
        if (i.value("name", "") == name)
            return &i;
    return nullptr;
}

static bool Contains(const std::vector<std::string> &v,
                     const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

static std::vector<std::string> Keys(
    const std::vector<ArgSpec> &specs) {
    std::vector<std::string> out;
    for (const auto &s : specs)
        out.push_back(s.key);
    return out;
}

static std::vector<std::string> RequiredKeys(
    const std::vector<ArgSpec> &specs) {
    std::vector<std::string> out;
    for (const auto &s : specs)
        if (s.required)
            out.push_back(s.key);
    return out;
}

int main() {
    json doc = Load("../configs/8bit.json");
    const json &add = *FindInst(doc, "ADD"); // encoding [5, "dest", "src"]

    // Argument specs match the loader's required-args table.
    assert(Keys(ArgSpecs("copy")) ==
           (std::vector<std::string>{"source", "dest"}));
    assert(RequiredKeys(ArgSpecs("alu")) ==
           (std::vector<std::string>{"op", "a", "out"}));
    assert(Keys(ArgSpecs("alu")) ==
           (std::vector<std::string>{"op", "a", "b", "c", "out",
                                     "update_flags"}));
    assert(RequiredKeys(ArgSpecs("mem_read")) ==
           (std::vector<std::string>{"addr", "out"}));
    assert(RequiredKeys(ArgSpecs("mem_write")) ==
           (std::vector<std::string>{"addr", "data"}));
    assert(RequiredKeys(ArgSpecs("port_read")) ==
           (std::vector<std::string>{"port", "out"}));
    assert(RequiredKeys(ArgSpecs("port_write")) ==
           (std::vector<std::string>{"port", "data"}));
    assert(RequiredKeys(ArgSpecs("coproc_read")) ==
           (std::vector<std::string>{"cp", "reg", "out"}));
    assert(RequiredKeys(ArgSpecs("coproc_write")) ==
           (std::vector<std::string>{"cp", "reg", "data"}));
    assert(RequiredKeys(ArgSpecs("branch")) ==
           (std::vector<std::string>{"target"}));
    assert(ArgSpecs("branch").size() == 3); // + optional condition, relative
    assert(ArgSpecs("halt").empty());
    assert(ArgSpecs("teleport").empty()); // unknown action: no fields
    // b/c are optional, and only `out`/`dest` slots are writable.
    for (const auto &s : ArgSpecs("alu")) {
        if (s.key == "b" || s.key == "c")
            assert(!s.required && s.kind == ArgKind::kOperand &&
                   !s.writable);
        if (s.key == "out")
            assert(s.required && s.writable);
        if (s.key == "op")
            assert(s.kind == ArgKind::kAluOp);
    }

    // Writable operand options: only writable targets, never $NEXT_PC
    // and never an immediate operand such as @imm8.
    {
        std::vector<std::string> opts = OperandOptions(doc, add, true);
        assert(Contains(opts, "@dest"));
        assert(Contains(opts, "@src"));
        assert(Contains(opts, "$PC"));
        assert(!Contains(opts, "$NEXT_PC"));
        assert(!Contains(opts, "@imm8"));
        assert(!Contains(opts, "@address"));
        assert(!Contains(opts, "@imm16"));
    }

    // Readable operand options: the encoding operands, $NEXT_PC and the
    // configured registers.
    {
        std::vector<std::string> opts = OperandOptions(doc, add, false);
        assert(Contains(opts, "@dest"));
        assert(Contains(opts, "@src"));
        assert(Contains(opts, "$PC"));
        assert(Contains(opts, "$SP"));
        assert(Contains(opts, "$FLAGS"));
        assert(Contains(opts, "$NEXT_PC"));
        assert(Contains(opts, "$R0"));
        assert(Contains(opts, "$R3"));
    }

    // An instruction with an immediate operand: readable offers @imm8,
    // writable does not.
    {
        json inst = json::parse(
            R"({"name":"LDI","opcode":9,"encoding":[9,"dest","imm8"]})");
        assert(Contains(OperandOptions(doc, inst, false), "@imm8"));
        assert(!Contains(OperandOptions(doc, inst, true), "@imm8"));
        assert(Contains(OperandOptions(doc, inst, true), "@dest"));
    }

    // ALU operation options are exactly the defined operations.
    assert(AluOpNames(doc) == (std::vector<std::string>{
                                   "ADD", "AND", "NOT", "OR", "SUB", "XOR"}));

    // Branch condition options: flag names and types, plain and negated.
    {
        std::vector<std::string> conds = ConditionOptions(doc);
        assert(Contains(conds, "Z"));
        assert(Contains(conds, "!Z"));
        assert(Contains(conds, "zero"));
        assert(Contains(conds, "!zero"));
        assert(Contains(conds, "C"));
        assert(Contains(conds, "carry"));
        assert(!Contains(conds, "NOFLAG"));
    }

    // Defaults prefill a missing required argument with the first
    // valid option.
    {
        std::vector<ArgSpec> alu = ArgSpecs("alu");
        assert(DefaultValue(doc, add, alu[0]) == "ADD"); // op
        assert(DefaultValue(doc, add, alu[1]) == "@dest"); // a
        assert(DefaultValue(doc, add, alu[4]) == "@dest"); // out
        assert(DefaultValue(doc, add, alu[2]).empty() == false); // b
        assert(DefaultValue(doc, add, alu[5]).empty()); // update_flags
    }

    // Text box visibility: an unset optional argument shows "(not
    // set)" and no box; a picker value shows no box; a literal or a
    // custom value does.
    {
        std::vector<ArgSpec> alu = ArgSpecs("alu");
        const ArgSpec &b = alu[2];   // b (optional), operand
        const ArgSpec &out = alu[4]; // out, writable operand
        assert(!ShowsTextBox(doc, add, b, ""));      // unset: no box
        assert(!ShowsTextBox(doc, add, b, "@src"));  // picker value
        assert(ShowsTextBox(doc, add, b, "#5"));     // literal
        assert(ShowsTextBox(doc, add, b, "@nope"));  // custom value
        assert(!ShowsTextBox(doc, add, out, "@dest"));
        assert(ShowsTextBox(doc, add, out, "$NEXT_PC")); // not writable
        assert(!ShowsTextBox(doc, add, alu[5], "")); // bool: never
        assert(!ShowsTextBox(doc, add, alu[5], "true"));
    }

    std::cout << "test_microop_editor: all assertions passed\n";
    return 0;
}
