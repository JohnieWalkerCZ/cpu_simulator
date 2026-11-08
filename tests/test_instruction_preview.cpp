// Tests for the configurator's instruction preview
// (src/ui/instruction_preview.hpp): assembly form, per-micro-op
// inputs/outputs, effective latency, and the two latency warnings.
// Errors (unresolved references, unknown ALU op, missing args, unknown
// action, encoding/opcode mismatch, no microcode) are NOT preview
// warnings anymore: they live once, with a fix, in the Problems list
// (see test_config_diagnostics.cpp). Runs from the tests/ working
// directory, so configs live at ../configs/8bit.json.
#include "ui/instruction_preview.hpp"
#include <cassert>
#include <fstream>
#include <iostream>

using nlohmann::json;
using namespace InstructionPreview;

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

int main() {
    json doc = Load("../configs/8bit.json");

    // ADD: single alu micro-op, no latency set, so dynamic.
    {
        Preview p = Build(doc, *FindInst(doc, "ADD"));
        assert(p.assembly == "ADD dest, src");
        assert(p.uops.size() == 1);
        assert(p.uops[0].action == "alu");
        assert(p.uops[0].summary.find("ADD(@dest, @src)") !=
               std::string::npos);
        assert(p.uops[0].summary.find("-> @dest") != std::string::npos);
        assert(p.uops[0].outputs.size() == 2); // @dest + FLAGS
        assert(p.uop_latency_sum == 1);        // ADD op latency 1
        assert(p.mode_effective == "dynamic");
        assert(p.effective_cycles == 1);
        assert(p.warnings.size() == 1); // latency-ignored warning
    }

    // PUSH: SUB alu (latency 3 in 8bit.json) + mem_write (1) = 4.
    {
        Preview p = Build(doc, *FindInst(doc, "PUSH"));
        assert(p.assembly == "PUSH src");
        assert(p.uops.size() == 2);
        assert(p.uops[0].latency == 3);
        assert(p.uops[1].latency == 1);
        assert(p.uop_latency_sum == 4);
        assert(p.effective_cycles == 4);
        assert(p.uops[1].summary.find("mem[$SP]") != std::string::npos);
        assert(p.warnings.size() == 1);
    }

    // HLT: single halt micro-op, 1 cycle, no inputs/outputs.
    {
        Preview p = Build(doc, *FindInst(doc, "HLT"));
        assert(p.assembly == "HLT");
        assert(p.uops.size() == 1 && p.uops[0].action == "halt");
        assert(p.uop_latency_sum == 1 && p.effective_cycles == 1);
        assert(p.uops[0].inputs.empty() && p.uops[0].outputs.empty());
    }

    // CALL: SUB (3) + mem_write (1) + branch (1) = 5; branch writes PC.
    {
        Preview p = Build(doc, *FindInst(doc, "CALL"));
        assert(p.uop_latency_sum == 5);
        assert(p.effective_cycles == 5);
        assert(p.uops[2].action == "branch");
        assert(p.uops[2].outputs.size() == 1 &&
               p.uops[2].outputs[0] == "PC");
    }

    // JZ: conditional branch.
    {
        Preview p = Build(doc, *FindInst(doc, "JZ"));
        assert(p.uops.size() == 1 && p.uops[0].action == "branch");
        assert(p.uops[0].summary.find("PC = @address") !=
               std::string::npos);
    }

    // Latency modes on a synthetic instruction (alu ADD, latency 1).
    {
        json inst = json::parse(
            R"({"name":"T","opcode":99,"encoding":[99,"dest"],"latency":3,"latency_mode":"strict","microcode":[{"action":"alu","op":"ADD","a":"@dest","b":"#1","out":"@dest"}]})");
        Preview p = Build(doc, inst);
        assert(p.mode_effective == "strict" && p.effective_cycles == 3);
        assert(p.warnings.empty());
        inst["latency_mode"] = "bottleneck";
        p = Build(doc, inst);
        assert(p.mode_effective == "bottleneck" &&
               p.effective_cycles == 3);
        inst["latency_mode"] = "additive";
        p = Build(doc, inst);
        assert(p.mode_effective == "additive" && p.effective_cycles == 4);
        // "dynamic" with a latency set is mapped to bottleneck by the
        // loader, and the preview warns about it.
        inst["latency_mode"] = "dynamic";
        p = Build(doc, inst);
        assert(p.mode_effective == "bottleneck" &&
               p.effective_cycles == 3);
        assert(p.warnings.size() == 1);
        // No latency: mode ignored, dynamic forced.
        inst.erase("latency");
        p = Build(doc, inst);
        assert(p.mode_effective == "dynamic" && p.effective_cycles == 1);
        assert(p.warnings.size() == 1);
        // Unknown mode string also maps to bottleneck.
        inst["latency_mode"] = "nonsense";
        inst["latency"] = 2;
        p = Build(doc, inst);
        assert(p.mode_effective == "bottleneck" &&
               p.effective_cycles == 2);
    }

    // Encoding/opcode mismatch: ADD with the opcode renumbered to 42
    // but the encoding still starting with 5. The mismatch is NOT a
    // preview warning anymore (the Problems list reports it once, with
    // a fix); the only preview warning left is the latency one.
    {
        json inst = *FindInst(doc, "ADD");
        inst["opcode"] = 42;
        Preview p = Build(doc, inst);
        assert(p.warnings.size() == 1);
        assert(p.warnings[0].find("Encoding") == std::string::npos);
        assert(p.warnings[0].find("opcode") == std::string::npos);
        inst["encoding"][0] = 42;
        p = Build(doc, inst);
        assert(p.warnings.size() == 1); // only the latency warning
    }

    // Broken references: @nope, $NOPE, unknown ALU op. None of these
    // is a preview warning anymore (the Problems list reports them);
    // the preview still derives the table, and the unknown ALU op is
    // still assumed to take 1 cycle.
    {
        json inst = json::parse(
            R"({"name":"BAD","opcode":98,"encoding":[98,"dest"],"microcode":[{"action":"alu","op":"NOPE","a":"@nope","b":"$NOPE","out":"@dest"},{"action":"copy","source":"@dest","dest":"$ALSO_NOPE"}]})");
        Preview p = Build(doc, inst);
        assert(p.warnings.size() == 1); // only the latency warning
        assert(p.uops[0].latency == 1); // unknown op assumed 1 cycle
    }

    // Honza's screenshot case: instruction "PRG src", encoding
    // [7, "src"], one alu micro-op with out = @sr (a typo for @src),
    // latency 2, strict mode. The Preview must not warn about @sr at
    // all (the Problems list reports it exactly once -- see
    // test_config_diagnostics.cpp); with a latency set and a mode the
    // loader honours, the preview has no warnings left.
    {
        json inst = json::parse(
            R"({"name":"PRG","opcode":7,"encoding":[7,"src"],"latency":2,"latency_mode":"strict","microcode":[{"action":"alu","op":"ADD","a":"@src","out":"@sr"}]})");
        Preview p = Build(doc, inst);
        assert(p.warnings.empty());
        assert(p.uops.size() == 1);
        assert(p.effective_cycles == 2);
    }

    std::cout << "All instruction preview tests passed!" << std::endl;
    return 0;
}
