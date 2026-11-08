// Pure (no ImGui) logic for duplicating one item (an instruction, a
// register, an ALU operation or a peripheral) in the configurator.
//
// What lives here (testable without a GUI):
//   - UniqueCopyName(existing_names, base): "<base> copy", or
//     "<base> copy 2", "<base> copy 3", ... when those are taken --
//     the first free name in that sequence. A base that already ends
//     in " copy" is treated literally: duplicating "ADD copy" yields
//     "ADD copy copy" (then "ADD copy copy 2", ...).
//   - DuplicateItem(item, unique_name): a deep copy of the JSON item
//     with "name" replaced by unique_name. nlohmann::json copies are
//     deep, so mutating the copy never touches the original.
//
// What lives in the UI, NOT here (the UI owns the document and its
// helpers): after duplicating an instruction the UI assigns the first
// free opcode via the existing FirstFreeCode helper in
// ui_configurator.hpp and syncs the encoding via
// SyncEncodingWithOpcode; after duplicating an ALU operation the UI
// likewise assigns the first free code (a duplicate code would fail
// Config::validate(), which rejects duplicate ALU codes). Names are
// unique-checked by the same validator for instructions, ALU
// operations and peripherals, which is why the copy always gets a
// fresh name from UniqueCopyName.
//
// Names are at global scope exactly as the Group 3 brief specifies,
// so the UI code in ui_configurator.hpp can call them unqualified.

#pragma once

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace DuplicateDetail {

// "name" is only meaningful on JSON objects; DuplicateItem leaves
// non-objects untouched (still a deep copy).
inline bool IsNameable(const nlohmann::json &item) {
    return item.is_object();
}

} // namespace DuplicateDetail

// First free "<base> copy" name: "<base> copy" when free, else
// "<base> copy 2", "<base> copy 3", ... (lowest free number wins, so
// a gap is reused: with "X copy" and "X copy 3" taken, "X copy 2"
// is returned).
inline std::string UniqueCopyName(
    const std::vector<std::string> &existing_names,
    const std::string &base) {
    const std::string first = base + " copy";
    if (std::find(existing_names.begin(), existing_names.end(), first) ==
        existing_names.end())
        return first;
    for (int n = 2;; ++n) {
        const std::string candidate =
            base + " copy " + std::to_string(n);
        if (std::find(existing_names.begin(), existing_names.end(),
                      candidate) == existing_names.end())
            return candidate;
    }
}

// Deep copy of `item` with its "name" replaced by `unique_name`.
// The original is untouched; nested arrays/objects in the copy are
// independent values. Non-object items are returned as an unchanged
// deep copy (there is no "name" to replace).
inline nlohmann::json DuplicateItem(const nlohmann::json &item,
                                    const std::string &unique_name) {
    nlohmann::json copy = item; // nlohmann::json copies are deep
    if (DuplicateDetail::IsNameable(copy))
        copy["name"] = unique_name;
    return copy;
}
