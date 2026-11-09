#pragma once
// Pure (no ImGui) example content for the configurator's "Insert
// example ..." buttons (Group 4, Clarity). The UI calls these builders
// and inserts the returned JSON into the document; nothing here knows
// about widgets, undo or selection.
//
// Shapes follow CONFIGURATION.md 6.3 (AST nodes: if / assign / call)
// and the shipped configs/8bit.json exactly:
//   - ExampleHook() is byte-for-byte the MathCoProcessor CMD on_write
//     (two `if` nodes dispatching on `value`).
//   - ExamplePeripheral() is a timer-like declarative peripheral in
//     the SystemTimer shape (a single-assign tick_behavior), with one
//     register whose on_write is a single `if` node in the same shape
//     as the MathCoProcessor one, plus an internal_state the user can
//     refer to from hook expressions.

#include <nlohmann/json.hpp>
#include <string>

namespace ExampleItems {

// A small declarative timer-like peripheral named `name`: one
// 1-byte register COUNT at offset 0 (rw, initial 0) covering
// 0x3000-0x3003, internal_state {"LIMIT": 10}, an on_write hook that
// resets COUNT to 0 when 1 is written to it, and a tick_behavior that
// counts up by 1 on every tick.
inline nlohmann::json ExamplePeripheral(const std::string &name) {
    nlohmann::json p;
    p["name"] = name;
    p["type"] = "declarative";
    p["address_start"] = "0x3000";
    p["address_end"] = "0x3003";
    p["internal_state"] = {{"LIMIT", 10}};
    nlohmann::json reg;
    reg["name"] = "COUNT";
    reg["offset"] = 0;
    reg["size_bytes"] = 1;
    reg["access"] = "rw";
    reg["initial"] = 0;
    reg["on_write"] = nlohmann::json::array(
        {{{"type", "if"},
          {"condition", "value == 1"},
          {"then",
           nlohmann::json::array(
               {{{"type", "assign"}, {"target", "COUNT"}, {"expr", "0"}}})}}});
    p["registers"] = nlohmann::json::array({reg});
    p["tick_behavior"] = nlohmann::json::array(
        {{{"type", "assign"}, {"target", "COUNT"}, {"expr", "COUNT + 1"}}});
    return p;
}

// The MathCoProcessor-style sample hook: writing 1 to the register
// computes OP_A * OP_B into RESULT, writing 2 computes OP_A / OP_B.
// Identical to the CMD on_write in configs/8bit.json, so it drops
// into any peripheral that has OP_A, OP_B and RESULT registers (and
// still parses anywhere else -- unknown targets only fail at load
// validation, like any hand-typed hook).
inline nlohmann::json ExampleHook() {
    return nlohmann::json::array(
        {{{"type", "if"},
          {"condition", "value == 1"},
          {"then",
           nlohmann::json::array(
               {{{"type", "assign"},
                 {"target", "RESULT"},
                 {"expr", "OP_A * OP_B"}}})}},
         {{"type", "if"},
          {"condition", "value == 2"},
          {"then",
           nlohmann::json::array(
               {{{"type", "assign"},
                 {"target", "RESULT"},
                 {"expr", "OP_A / OP_B"}}})}}});
}

} // namespace ExampleItems
