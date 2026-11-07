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

// Every issue must carry a non-empty problem and a non-empty fix.
static void AssertProblemAndFix(const std::vector<Issue> &issues) {
    for (const auto &issue : issues) {
        assert(!issue.where.empty());
        assert(!issue.problem.empty());
        assert(!issue.fix.empty());
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
        assert(HasIssue(issues, "ALU code 1", "\"code\""));
    }

    // data_bus width not a multiple of 4.
    {
        json doc = base;
        doc["data_bus"]["width"] = 10;
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(HasIssue(issues, "data_bus.width is 10",
                        "\"data_bus.width\""));
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
    }

    // Duplicate flag bit.
    {
        json doc = base;
        doc["alu"]["flags"][1]["bit"] = doc["alu"]["flags"][0]["bit"];
        std::vector<Issue> issues = Diagnose(doc);
        AssertProblemAndFix(issues);
        assert(HasIssue(issues, "Flag bit 0", "\"bit\""));
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
