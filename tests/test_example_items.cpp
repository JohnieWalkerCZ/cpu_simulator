// Tests for the example peripheral / hook builders
// (src/ui/example_items.hpp): the shapes the "Insert example ..."
// buttons insert. Runs from the tests/ working directory, so configs
// live at ../configs/8bit.json.
#include "ui/example_items.hpp"
#include <cassert>
#include <fstream>
#include <iostream>

using nlohmann::json;
using namespace ExampleItems;

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

int main() {
    // ExamplePeripheral: type, address range, internal state.
    json p = ExamplePeripheral("ExampleTimer");
    assert(p.is_object());
    assert(p["name"] == "ExampleTimer");
    assert(p["type"] == "declarative");
    assert(p["address_start"] == "0x3000");
    assert(p["address_end"] == "0x3003");
    assert(p["internal_state"] == json::parse(R"({"LIMIT": 10})"));

    // One register, COUNT at offset 0, 1 byte, rw, initial 0.
    assert(p["registers"].is_array() && p["registers"].size() == 1);
    const json &reg = p["registers"][0];
    assert(reg.is_object());
    assert(reg["name"] == "COUNT" && reg["offset"] == 0 &&
           reg["size_bytes"] == 1 && reg["access"] == "rw" &&
           reg["initial"] == 0);

    // tick_behavior: a one-node assign array, COUNT = COUNT + 1.
    assert(p["tick_behavior"].is_array() &&
           p["tick_behavior"].size() == 1);
    const json &tick = p["tick_behavior"][0];
    assert(tick.is_object() && tick["type"] == "assign" &&
           tick["target"] == "COUNT" && tick["expr"] == "COUNT + 1");

    // on_write: a one-node if array; then holds one assign, and there
    // is no else branch (same shape as the shipped MathCoProcessor).
    assert(reg["on_write"].is_array() && reg["on_write"].size() == 1);
    const json &ifnode = reg["on_write"][0];
    assert(ifnode.is_object() && ifnode["type"] == "if" &&
           ifnode["condition"] == "value == 1");
    assert(ifnode["then"].is_array() && ifnode["then"].size() == 1);
    assert(ifnode["then"][0].is_object() &&
           ifnode["then"][0]["type"] == "assign" &&
           ifnode["then"][0]["target"] == "COUNT" &&
           ifnode["then"][0]["expr"] == "0");
    assert(!ifnode.contains("else"));

    // The name parameter is honoured (the UI passes the unique name).
    assert(ExamplePeripheral("ExampleTimer 2")["name"] ==
           "ExampleTimer 2");

    // ExampleHook: a two-node array of if nodes.
    json h = ExampleHook();
    assert(h.is_array() && h.size() == 2);
    for (const auto &node : h) {
        assert(node.is_object());
        assert(node.contains("type"));
    }
    assert(h[0]["type"] == "if" && h[0]["condition"] == "value == 1");
    assert(h[0]["then"].is_array() && h[0]["then"].size() == 1);
    assert(h[0]["then"][0].is_object() &&
           h[0]["then"][0]["type"] == "assign" &&
           h[0]["then"][0]["target"] == "RESULT" &&
           h[0]["then"][0]["expr"] == "OP_A * OP_B");
    assert(!h[0].contains("else"));
    assert(h[1]["type"] == "if" && h[1]["condition"] == "value == 2");
    assert(h[1]["then"].is_array() && h[1]["then"].size() == 1);
    assert(h[1]["then"][0].is_object() &&
           h[1]["then"][0]["type"] == "assign" &&
           h[1]["then"][0]["target"] == "RESULT" &&
           h[1]["then"][0]["expr"] == "OP_A / OP_B");
    assert(!h[1].contains("else"));

    // ExampleHook is exactly the shipped MathCoProcessor CMD on_write.
    json doc = Load("../configs/8bit.json");
    const json *math = FindPeriph(doc, "MathCoProcessor");
    assert(math != nullptr);
    const json *cmd = FindPeriphReg(*math, "CMD");
    assert(cmd != nullptr);
    assert((*cmd)["on_write"] == h);

    // Both builders' hooks are JSON arrays of objects with a "type"
    // field, as CONFIGURATION.md 6.3 requires.
    for (const auto &node : p["tick_behavior"]) {
        assert(node.is_object() && node.contains("type"));
    }
    for (const auto &node : reg["on_write"]) {
        assert(node.is_object() && node.contains("type"));
    }
    for (const auto &node : h) {
        assert(node.is_object() && node.contains("type"));
    }

    std::cout << "All example items tests passed!" << std::endl;
    return 0;
}
