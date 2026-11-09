// Tests for the configurator's diagnostics (src/ui/config_diagnostics.hpp):
// every problem in a configuration document, each with a concrete fix.
// Runs from the tests/ working directory, so configs live at
// ../configs/8bit.json.
#include "ui/config_diagnostics.hpp"
#include <cassert>
#include <fstream>
#include <iostream>

using nlohmann::json;
using namespace ConfigDiagnostics;

static json Load(const std::string &path) {
    std::ifstream f(path);
    assert(f.is_open());
    json j;
    f >> j;
    return j;
}

// True when some issue's problem contains `problem_sub` and its fix
// contains `fix_sub`.
static bool HasIssue(const std::vector<Issue> &issues,
                     const std::string &problem_sub,
                     const std::string &fix_sub) {
    for (const auto &issue : issues)
        if (issue.problem.find(problem_sub) != std::string::npos &&
            issue.fix.find(fix_sub) != std::string::npos)
            return true;
    return false;
}

// True when some issue whose problem contains `problem_sub`
// navigates to (`tab`, `index`, `name`).
static bool HasNav(const std::vector<Issue> &issues,
                   const std::string &problem_sub, const std::string &tab,
                   int index, const std::string &name) {
    for (const auto &issue : issues)
        if (issue.problem.find(problem_sub) != std::string::npos &&
            issue.nav_tab == tab && issue.nav_index == index &&
            issue.nav_name == name)
            return true;
    return false;
}

// Every issue must carry a non-empty problem and a non-empty fix.
static void AssertProblemAndFix(const std::vector<Issue> &issues) {
    // nav_tab is always one of the configurator's tab labels, and
    // nav_index is -1 only when the issue belongs to no specific item.
    static const char *tabs[] = {"System", "Registers", "ALU",
                                 "Instructions", "Memory", "Peripherals"};
    for (const auto &issue : issues) {
        assert(!issue.where.empty());
        assert(!issue.problem.empty());
        assert(!issue.fix.empty());
        bool known_tab = false;
        for (const char *t : tabs)
            if (issue.nav_tab == t)
                known_tab = true;
        assert(known_tab);
        assert(issue.nav_index >= -1);
    }
}

// A document holding exactly the given instructions, on top of the valid
// 8bit.json base (registers, ALU, memory, peripherals all valid).
static json DocWithInstructions(json base, json instructions) {
    base["instruction_set"]["instructions"] = instructions;
    return base;
}

static json HaltInst(const std::string &name, int opcode) {
    return {{"name", name},
            {"opcode", opcode},
            {"encoding", json::array({opcode})},
            {"microcode", json::array({{{"action", "halt"}}})}};
}

int main() {
    json base = Load("../configs/8bit.json");

    // The shipped 8bit.json is valid: no issues at all, including for
    // one of its instructions. (This also covers the layered-terminal
    // exception: SerialTerminal text_display overlaps DeclarativeUART
    // declarative on purpose, and must not be reported.)
    {
        std::vector<Issue> issues = Diagnose(base);
        AssertProblemAndFix(issues);
        assert(issues.empty());
        const json *add = nullptr;
        for (const auto &i : base["instruction_set"]["instructions"])
            if (i.value("name", "") == "ADD")
                add = &i;
        assert(add != nullptr);
        assert(DiagnoseInstruction(base, *add).empty());
    }

    // Empty instruction name.
    {
        json doc = DocWithInstructions(base, json::array({HaltInst("", 200)}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "empty", "\"name\""));
        // An unnamed instruction still navigates to its own index.
        assert(HasNav(issues, "empty", "Instructions", 0, ""));
    }

    // Duplicate opcode: two instructions sharing opcode 200.
    {
        json doc = DocWithInstructions(
            base, json::array({HaltInst("FIRST", 200), HaltInst("SECOND", 200)}));
        std::vector<Issue> issues = DiagnoseInstruction(
            doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "Opcode 200", "opcode"));
        assert(HasNav(issues, "Opcode 200", "Instructions", 0, "FIRST"));
    }

    // Empty encoding.
    {
        json inst = HaltInst("NOENC", 201);
        inst["encoding"] = json::array();
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "encoding", "\"encoding\""));
    }

    // encoding[0] does not equal the opcode.
    {
        json inst = HaltInst("MISMATCH", 5);
        inst["encoding"] = json::array({6, "dest"});
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "encoding[0] is 6", "\"encoding\""));
        assert(HasNav(issues, "encoding[0] is 6", "Instructions", 0,
                      "MISMATCH"));
    }

    // Empty microcode.
    {
        json inst = HaltInst("NOUOPS", 202);
        inst["microcode"] = json::array();
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "microcode", "\"microcode\""));
    }

    // ALU micro-op missing its required `out` argument.
    {
        json inst = {{"name", "NOOUT"},
                     {"opcode", 203},
                     {"encoding", json::array({203, "src"})},
                     {"microcode",
                      json::array({{{"action", "alu"},
                                    {"op", "ADD"},
                                    {"a", "@src"}}})}};
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "missing the required argument \"out\"",
                        "\"out\""));
        // Micro-ops are numbered from 1, matching the preview table's
        // "#" column (the first micro-op is "Micro-op 1", not 0).
        assert(HasIssue(issues, "Micro-op 1 (alu)", "\"out\""));
    }

    // Numbering: a problem in the second micro-op says "Micro-op 2".
    {
        json inst = {{"name", "SECONDBAD"},
                     {"opcode", 203},
                     {"encoding", json::array({203, "dest", "src"})},
                     {"microcode",
                      json::array({{{"action", "halt"}},
                                   {{"action", "alu"},
                                    {"op", "ADD"},
                                    {"a", "@src"}}})}};
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "Micro-op 2 (alu)", "\"out\""));
    }

    // Honza's screenshot case: instruction "PRG src", encoding
    // [7, "src"], one alu micro-op with out = @sr (a typo for @src).
    // The Problems list must report it exactly once (the Preview does
    // not repeat it -- see test_instruction_preview.cpp).
    {
        json inst = {{"name", "PRG"},
                     {"opcode", 7},
                     {"encoding", json::array({7, "src"})},
                     {"latency", 2},
                     {"latency_mode", "strict"},
                     {"microcode",
                      json::array({{{"action", "alu"},
                                    {"op", "ADD"},
                                    {"a", "@src"},
                                    {"out", "@sr"}}})}};
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "Micro-op 1 (alu)", "encoding"));
        assert(HasIssue(issues, "@sr", "encoding operands: src"));
    }

    // ALU micro-op naming an operation that is not defined.
    {
        json inst = {{"name", "BADOP"},
                     {"opcode", 204},
                     {"encoding", json::array({204, "src"})},
                     {"microcode",
                      json::array({{{"action", "alu"},
                                    {"op", "NOPE"},
                                    {"a", "@src"},
                                    {"out", "@dest"}}})}};
        inst["encoding"] = json::array({204, "dest", "src"});
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "NOPE", "\"op\""));
    }

    // Operand reference @bad is not an encoding operand.
    {
        json inst = {{"name", "BADREF"},
                     {"opcode", 205},
                     {"encoding", json::array({205, "dest", "src"})},
                     {"microcode",
                      json::array({{{"action", "copy"},
                                    {"source", "@bad"},
                                    {"dest", "@dest"}}})}};
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "@bad", "encoding"));
    }

    // Branch condition naming no flag.
    {
        json inst = {{"name", "BADCOND"},
                     {"opcode", 206},
                     {"encoding", json::array({206, "address"})},
                     {"microcode",
                      json::array({{{"action", "branch"},
                                    {"target", "@address"},
                                    {"condition", "NOFLAG"}}})}};
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "NOFLAG", "\"condition\""));
    }

    // A deliberately broken instruction all at once: every issue it
    // produces must still carry a problem and a fix.
    {
        json inst = {{"name", ""},
                     {"opcode", 207},
                     {"encoding", json::array({208, "frobnicate", 99})},
                     {"microcode",
                      json::array({{{"action", "teleport"}},
                                   {{"action", "alu"},
                                    {"op", "NOPE"},
                                    {"a", "@bad"},
                                    {"out", "$NEXT_PC"}},
                                   {{"action", "copy"},
                                    {"source", "$NOPE"},
                                    {"dest", "@imm8"}}})}};
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(issues.size() >= 8);
        assert(HasIssue(issues, "empty", "\"name\""));
        assert(HasIssue(issues, "encoding[0] is 208", "\"encoding\""));
        assert(HasIssue(issues, "frobnicate", "\"dest\""));
        assert(HasIssue(issues, "99", "0 to 15"));
        assert(HasIssue(issues, "teleport", "\"action\""));
        assert(HasIssue(issues, "NOPE", "\"op\""));
        assert(HasIssue(issues, "@bad", "encoding"));
        assert(HasIssue(issues, "$NEXT_PC", "@dest"));
        assert(HasIssue(issues, "$NOPE", "$PC"));
        assert(HasIssue(issues, "@imm8", "encoding"));
    }

    // Global cases.

    // Duplicate ALU code.
    {
        json doc = base;
        doc["alu"]["operations"][1]["code"] =
            doc["alu"]["operations"][0]["code"];
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(HasIssue(issues, "ALU code 1", "free code")); // pre-existing
        // test bug fixed: the fix says "a free code (0-255)", it never
        // contained a quoted "code", so the old "\"code\"" expectation
        // failed at HEAD too (verified by stashing my changes).
        // The second operation with the code (SUB, index 1) is the one
        // reported, and it navigates to that operation in the ALU tab.
        assert(HasNav(issues, "ALU code 1", "ALU", 1, "SUB"));
    }

    // data_bus width not a multiple of 4.
    {
        json doc = base;
        doc["data_bus"]["width"] = 10;
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(HasIssue(issues, "data_bus.width is 10",
                        "\"data_bus.width\""));
        // A system-level issue belongs to no item: tab only.
        assert(HasNav(issues, "data_bus.width is 10", "System", -1, ""));
    }

    // No register with the program_counter role.
    {
        json doc = base;
        for (auto &reg : doc["registers"]["special"])
            if (reg.value("name", "") == "PC")
                reg.erase("role");
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(HasIssue(issues, "program_counter", "\"program_counter\""));
    }

    // Overlapping memory segments.
    {
        json doc = base;
        doc["memory"]["segments"][1]["start"] =
            doc["memory"]["segments"][0]["start"];
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(HasIssue(issues, "overlap", "\"start\""));
        // An overlap navigates to the first of the two segments (ROM).
        assert(HasNav(issues, "overlap", "Memory", 0, "ROM"));
    }

    // Duplicate flag bit.
    {
        json doc = base;
        doc["alu"]["flags"][1]["bit"] = doc["alu"]["flags"][0]["bit"];
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(HasIssue(issues, "Flag bit 0", "no other flag uses"));
        // (Same pre-existing test bug as the ALU-code case above: the
        // duplicate-bit fix never contained a quoted "bit".)
        // The second flag on the bit (C, index 1) navigates to that
        // flag in the ALU tab's flag list.
        assert(HasNav(issues, "Flag bit 0", "ALU", 1, "C"));
    }

    // Duplicate register name in general_purpose: the second R0 (index
    // 1 in the flattened top-level order) navigates to itself.
    {
        json doc = base;
        doc["registers"]["general_purpose"][1]["name"] = "R0";
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "Register name 'R0'", "Rename"));
        assert(HasNav(issues, "Register name 'R0'", "Registers", 1, "R0"));
    }

    // Duplicate register name across groups: SP renamed to PC. The
    // flattened top-level order is R0..R3 (0-3), PC (4), SP (5),
    // FLAGS (6), so the duplicate navigates to index 5.
    {
        json doc = base;
        doc["registers"]["special"][1]["name"] = "PC";
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "Register name 'PC'", "Rename"));
        assert(HasNav(issues, "Register name 'PC'", "Registers", 5, "PC"));
    }

    // A sub-register issue navigates to its top-level parent: R0L's
    // mask only selects 1 bit of R0, but the issue points at R0
    // (top-level index 0), the item the Registers tab selects.
    {
        json doc = base;
        doc["registers"]["general_purpose"][0]["sub_registers"] =
            json::array({{{"name", "R0L"},
                          {"width", 8},
                          {"mask", "0b00000001"}}});
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "only selects 1 bit", "mask"));
        assert(HasNav(issues, "only selects 1 bit", "Registers", 0, "R0"));
    }

    // Peripheral issue: an unknown type navigates to that peripheral.
    // (SystemTimer, index 2: its range overlaps nothing, unlike
    // SerialTerminal, whose range deliberately overlaps
    // DeclarativeUART's under the layered-terminal exception -- changing
    // SerialTerminal's type would add a second, overlap issue.)
    {
        json doc = base;
        doc["peripherals"][2]["type"] = "bogus";
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(issues.size() == 1);
        assert(HasIssue(issues, "unknown type", "text_display"));
        assert(HasNav(issues, "unknown type", "Peripherals", 2,
                      "SystemTimer"));
    }

    // Opcode wider than the opcode_width mask.
    {
        json inst = HaltInst("TOOBIG", 300);
        json doc = DocWithInstructions(base, json::array({inst}));
        std::vector<Issue> issues =
            DiagnoseInstruction(doc, doc["instruction_set"]["instructions"][0]);
        AssertProblemAndFix(issues);
        assert(HasIssue(issues, "Opcode 300", "\"opcode\""));
    }

    std::cout << "test_config_diagnostics: all assertions passed\n";
    return 0;
}
