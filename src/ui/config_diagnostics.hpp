#pragma once
// Pure (no ImGui) diagnostics for a CPU configuration document. The
// configurator renders the result; the logic lives here so it can be
// tested without a GUI.
//
// What it does: mirror the loader's rules (Config::from_json +
// Config::validate() in src/core/config.cpp) on the raw JSON document
// and report every problem it finds, each with a concrete fix naming
// the field and the value or range to use. Unlike validate(), which
// only returns a bool, Diagnose() returns the whole list, so the UI can
// show all of them at once instead of only the first one.
//
// Issue.where is "Area / location", e.g. "Instructions / 'ADD' (#0)"
// or "Memory / segment 'RAM'". AreaOf() returns the part before " / ",
// which the Problems tab uses to group issues.
//
// Notes on fidelity: where a rule depends on parsing that the loader
// does (e.g. a register's flattened bit mapping), the check is done on
// the JSON the same way the parser does it. Rules that cannot be
// mirrored from JSON are not reported here; the only loader rule
// skipped is that a flattened register's physical_index must be >= 0,
// which the parser guarantees by construction (it assigns the indices
// itself), so it can never fail on a document the parser accepted.

#include "core/alu/alu.hpp"
#include "core/wide_int.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ConfigDiagnostics {

struct Issue {
    std::string where;   // "Area / location", e.g. "ALU / operation 'ADD'"
    std::string problem; // what is wrong, with the offending values
    std::string fix;     // what to change, naming the field and value/range
    // Navigation target for the configurator UI: clicking the issue
    // switches to this tab and selects this item in the tab's list.
    // nav_tab must be one of the BeginTabItem labels in
    // ui_configurator.hpp, verbatim: "System", "Registers", "Memory",
    // "ALU", "Instructions" or "Peripherals". nav_index is the index
    // of the item in that tab's list, or -1 when the issue belongs to
    // no specific item (switch tab only). nav_name is the item's name
    // for display ("" when there is no specific item).
    //
    // Index conventions per tab (the UI maps these onto its selection
    // fields): Instructions = index in instruction_set.instructions
    // (sel_instruction); ALU operations = index in alu.operations
    // (sel_alu_op), ALU flags = index in alu.flags (sel_flag, told
    // apart from operations by the "ALU / flag ..." prefix of `where`);
    // Peripherals = index in peripherals (sel_peripheral); Memory =
    // index in memory.segments (sel_segment); Registers = index of the
    // top-level register in flattened document order
    // (general_purpose first, then special), which the UI splits into
    // (sel_group, sel_register) -- sub-register issues point at their
    // top-level parent; System issues have no item (nav_index -1).
    std::string nav_tab;
    int nav_index = -1;
    std::string nav_name;
};

// Area of an issue: the part of `where` before " / ".
inline std::string AreaOf(const Issue &issue) {
    const size_t pos = issue.where.find(" / ");
    if (pos == std::string::npos)
        return issue.where;
    return issue.where.substr(0, pos);
}

// Display form of a JSON scalar: strings are shown raw (so "0x2000"
// stays "0x2000"), numbers/booleans are dumped.
inline std::string ScalarText(const nlohmann::json &v) {
    if (v.is_string())
        return v.get<std::string>();
    return v.dump();
}

// Integer value of a JSON field, or `def` when it is missing or not an
// integer. (nlohmann's .value() throws on a type mismatch, so every read
// below goes through these helpers and Diagnose() never throws.)
inline int JsonInt(const nlohmann::json &obj, const char *key, int def) {
    if (!obj.is_object() || !obj.contains(key))
        return def;
    const auto &v = obj[key];
    try {
        if (v.is_number_integer())
            return v.get<int>();
        if (v.is_number_unsigned())
            return static_cast<int>(v.get<uint64_t>());
    } catch (...) {
        return def;
    }
    return def;
}

inline bool JsonHasInt(const nlohmann::json &obj, const char *key) {
    return obj.is_object() && obj.contains(key) &&
           (obj[key].is_number_integer() || obj[key].is_number_unsigned());
}

inline std::string JsonString(const nlohmann::json &obj, const char *key,
                              const std::string &def = "") {
    if (!obj.is_object() || !obj.contains(key) || !obj[key].is_string())
        return def;
    return obj[key].get<std::string>();
}

// Word value of a JSON scalar: strings are parsed as decimal/0x/0b
// literals (the loader's parse_word), numbers are taken as-is.
// Returns false when the value cannot be parsed.
inline bool TryWord(const nlohmann::json &v, word_t &out) {
    try {
        if (v.is_string()) {
            out = parse_word(v.get<std::string>());
            return true;
        }
        if (v.is_number_unsigned()) {
            out = static_cast<word_t>(v.get<uint64_t>());
            return true;
        }
        if (v.is_number_integer()) {
            const long long n = v.get<long long>();
            if (n < 0)
                return false;
            out = static_cast<word_t>(n);
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

// The loader's flag-expression check: only a, b, c, res and max_val may
// appear as identifiers (see valid_flag_expression in config.cpp).
inline bool ValidFlagExpression(const std::string &expression) {
    std::string normalized;
    for (size_t i = 0; i < expression.size();) {
        const unsigned char c =
            static_cast<unsigned char>(expression[i]);
        if (std::isalpha(c) || c == '_') {
            size_t end = i + 1;
            while (end < expression.size() &&
                   (std::isalnum(
                        static_cast<unsigned char>(expression[end])) ||
                    expression[end] == '_'))
                ++end;
            const std::string name = expression.substr(i, end - i);
            if (name != "a" && name != "b" && name != "c" &&
                name != "res" && name != "max_val")
                return false;
            normalized += '1';
            i = end;
        } else {
            normalized += expression[i++];
        }
    }
    return ALU::is_valid_expression(normalized);
}

// Smallest non-negative integer not in `used`, capped at `max_value`;
// -1 when the whole range is taken. Used in fix suggestions.
inline int FirstFree(const std::unordered_set<int> &used, int max_value) {
    for (int v = 0; v <= max_value; ++v)
        if (!used.count(v))
            return v;
    return -1;
}

// Encoding token strings as the loader maps them to field codes
// (see Config::from_json): dest -1, src -2, addr_reg -3, offset -4,
// imm8 -5, imm16 -6, address -7, imm -5. Anything else is -10 and the
// loader rejects the instruction.
inline int EncodingTokenCode(const std::string &token) {
    if (token == "dest")
        return -1;
    if (token == "src")
        return -2;
    if (token == "addr_reg")
        return -3;
    if (token == "offset")
        return -4;
    if (token == "imm8")
        return -5;
    if (token == "imm16")
        return -6;
    if (token == "address")
        return -7;
    if (token == "imm")
        return -5;
    return -10;
}

inline bool IsKnownRole(const std::string &role) {
    return role == "pc" || role == "sp" || role == "flags" ||
           role == "ir" || role == "accumulator" ||
           role == "program_counter" || role == "stack_pointer" ||
           role == "status_flags";
}

// Shared facts about the document, collected once so the per-area
// checks do not re-walk the JSON. Register facts come from a recursive
// walk of general_purpose + special (the loader's order); a
// sub-register inherits its parent's role when its own role is empty.
struct Context {
    int data_width = 0;
    bool data_width_ok = false;
    int addr_width = 0;
    bool addr_width_ok = false;
    int opcode_width = 8;
    bool has_memory_size = false;
    word_t memory_size = 0;
    bool memory_size_ok = false;

    struct RegInfo {
        std::string name;
        int width = 0;
        std::string role; // effective role (own, or inherited from parent)
    };
    std::vector<RegInfo> regs; // flattened, loader order
    std::unordered_set<std::string> reg_names;
    int flags_width = 0; // width of the (last) flags-role register, else 0
    std::unordered_set<std::string> flag_names;
    std::unordered_set<std::string> flag_types;
    std::unordered_set<std::string> alu_names;
    std::unordered_map<std::string, int> alu_latency; // name -> latency
    std::unordered_set<int> alu_codes;
    int reg_bits = 1; // bits of a register field in an encoding
};

inline Context BuildContext(const nlohmann::json &doc) {
    Context ctx;
    if (doc.contains("data_bus") && doc["data_bus"].is_object()) {
        ctx.data_width = JsonInt(doc["data_bus"], "width", 0);
        ctx.data_width_ok = ctx.data_width >= 4 && ctx.data_width <= 128 &&
                            ctx.data_width % 4 == 0;
    }
    if (doc.contains("address_bus") && doc["address_bus"].is_object()) {
        ctx.addr_width = JsonInt(doc["address_bus"], "width", 0);
        ctx.addr_width_ok = ctx.addr_width >= 4 && ctx.addr_width <= 128 &&
                            ctx.addr_width % 4 == 0;
    }
    if (doc.contains("instruction_set") &&
        doc["instruction_set"].is_object())
        ctx.opcode_width = JsonInt(doc["instruction_set"], "opcode_width",
                                   ctx.data_width >= 8 ? 8 : ctx.data_width);
    if (doc.contains("memory") && doc["memory"].is_object() &&
        doc["memory"].contains("size")) {
        ctx.has_memory_size = true;
        ctx.memory_size_ok =
            TryWord(doc["memory"]["size"], ctx.memory_size);
    }

    std::function<void(const nlohmann::json &, const std::string &)> walk =
        [&](const nlohmann::json &reg, const std::string &parent_role) {
            if (!reg.is_object())
                return;
            Context::RegInfo info;
            info.name = JsonString(reg, "name");
            info.width = JsonInt(reg, "width", 0);
            info.role = JsonString(reg, "role");
            if (info.role.empty())
                info.role = parent_role;
            ctx.regs.push_back(info);
            if (!info.name.empty())
                ctx.reg_names.insert(info.name);
            if (info.role == "flags" || info.role == "status_flags")
                ctx.flags_width = info.width;
            if (reg.contains("sub_registers") &&
                reg["sub_registers"].is_array())
                for (const auto &sub : reg["sub_registers"])
                    walk(sub, info.role);
        };
    if (doc.contains("registers") && doc["registers"].is_object()) {
        for (const char *group : {"general_purpose", "special"}) {
            if (!doc["registers"].contains(group) ||
                !doc["registers"][group].is_array())
                continue;
            for (const auto &reg : doc["registers"][group])
                walk(reg, "");
        }
    }
    ctx.reg_bits = ctx.regs.size() <= 1 ? 1 : 0;
    while ((static_cast<size_t>(1) << ctx.reg_bits) < ctx.regs.size())
        ++ctx.reg_bits;

    if (doc.contains("alu") && doc["alu"].is_object()) {
        if (doc["alu"].contains("flags") && doc["alu"]["flags"].is_array())
            for (const auto &f : doc["alu"]["flags"]) {
                ctx.flag_names.insert(JsonString(f, "name"));
                ctx.flag_types.insert(JsonString(f, "type"));
            }
        if (doc["alu"].contains("operations") &&
            doc["alu"]["operations"].is_array())
            for (const auto &op : doc["alu"]["operations"]) {
                ctx.alu_names.insert(JsonString(op, "name"));
                ctx.alu_latency[JsonString(op, "name")] =
                    JsonInt(op, "latency", 1);
                if (JsonHasInt(op, "code"))
                    ctx.alu_codes.insert(JsonInt(op, "code", 0));
            }
    }
    return ctx;
}

namespace Detail {

// Number of bits in the mask pattern of a sub-register, following
// parse_mask_pattern in config.cpp. Returns false when the mask cannot
// be parsed; `active` is how many parent bits the mask selects.
inline bool MaskActiveBits(const nlohmann::json &val, int reg_width,
                           int parent_width, int &active) {
    active = 0;
    word_t pattern = 0;
    int pattern_len = 0;
    try {
        if (val.is_number()) {
            pattern_len = reg_width;
            if (!TryWord(val, pattern))
                return false;
        } else if (!val.is_string()) {
            return false;
        } else {
            const std::string str = val.get<std::string>();
            if (str.compare(0, 2, "0b") == 0 ||
                str.compare(0, 2, "0B") == 0) {
                pattern_len = static_cast<int>(str.size() - 2);
                pattern = parse_word("0b" + str.substr(2));
            } else if (str.compare(0, 2, "0x") == 0 ||
                       str.compare(0, 2, "0X") == 0) {
                pattern_len = static_cast<int>((str.size() - 2) * 4);
                pattern = parse_word(str);
            } else {
                pattern_len = 8;
                pattern = parse_word(str);
            }
        }
    } catch (...) {
        return false;
    }
    if (pattern_len <= 0)
        return false;
    if (pattern_len > reg_width) {
        pattern_len = reg_width;
        pattern &= mask_for_width(pattern_len);
    }
    for (int i = 0; i < parent_width; ++i)
        if ((pattern >> (i % pattern_len)) & 1)
            ++active;
    return true;
}

inline void CheckSystem(const nlohmann::json &doc, const Context &ctx,
                        std::vector<Issue> &out) {
    // System issues belong to no specific item: the tab only.
    auto add = [&](const std::string &loc, const std::string &problem,
                   const std::string &fix) {
        out.push_back({"System / " + loc, problem, fix, "System", -1, ""});
    };
    if (!doc.contains("name") || !doc["name"].is_string() ||
        doc["name"].get<std::string>().empty())
        add("name", "The configuration has no \"name\".",
            "Set \"name\" to a non-empty string, e.g. \"8-Bit\".");
    if (!doc.contains("data_bus") || !doc["data_bus"].is_object() ||
        !doc["data_bus"].contains("width"))
        add("data_bus.width", "data_bus.width is missing.",
            "Set \"data_bus\": { \"width\": 8 } (a multiple of 4 from 4 "
            "to 128).");
    else if (!JsonHasInt(doc["data_bus"], "width"))
        add("data_bus.width", "data_bus.width is not a number.",
            "Set \"data_bus.width\" to a whole number, e.g. 8.");
    else if (!ctx.data_width_ok)
        add("data_bus.width",
            "data_bus.width is " + std::to_string(ctx.data_width) +
                ", but it must be a multiple of 4 between 4 and 128.",
            "Set \"data_bus.width\" to a multiple of 4 from 4 to 128, "
            "e.g. 8.");
    if (!doc.contains("address_bus") || !doc["address_bus"].is_object() ||
        !doc["address_bus"].contains("width"))
        add("address_bus.width", "address_bus.width is missing.",
            "Set \"address_bus\": { \"width\": 16 } (a multiple of 4 "
            "from 4 to 128).");
    else if (!JsonHasInt(doc["address_bus"], "width"))
        add("address_bus.width", "address_bus.width is not a number.",
            "Set \"address_bus.width\" to a whole number, e.g. 16.");
    else if (!ctx.addr_width_ok)
        add("address_bus.width",
            "address_bus.width is " + std::to_string(ctx.addr_width) +
                ", but it must be a multiple of 4 between 4 and 128.",
            "Set \"address_bus.width\" to a multiple of 4 from 4 to 128, "
            "e.g. 16.");
    if (doc.contains("instruction_set") &&
        doc["instruction_set"].is_object() &&
        doc["instruction_set"].contains("opcode_width")) {
        const int ow = ctx.opcode_width;
        if (!JsonHasInt(doc["instruction_set"], "opcode_width"))
            add("instruction_set.opcode_width",
                "instruction_set.opcode_width is not a number.",
                "Set \"opcode_width\" to a whole number from 1 to 16.");
        else if (ow < 1 || ow > 16 ||
                 (ctx.data_width_ok && ow > ctx.data_width))
            add("instruction_set.opcode_width",
                "opcode_width is " + std::to_string(ow) +
                    ", but it must be from 1 to 16 and no wider than the "
                    "data bus (" +
                    std::to_string(ctx.data_width) + " bits).",
                "Set \"opcode_width\" to a number from 1 to 16 that is "
                "at most the data-bus width, e.g. 8.");
    }
    if (!doc.contains("memory") || !doc["memory"].is_object())
        add("memory", "The \"memory\" object is missing.",
            "Add \"memory\": { \"size\": 65536 } (and \"segments\" if "
            "needed).");
    else {
        if (!doc["memory"].contains("size"))
            add("memory.size", "memory.size is missing.",
                "Set \"memory.size\" to the memory size in bytes, e.g. "
                "65536.");
        else if (!ctx.memory_size_ok)
            add("memory.size",
                "memory.size (\"" +
                    ScalarText(doc["memory"]["size"]) +
                    "\") cannot be parsed as a number.",
                "Set \"memory.size\" to a decimal number or a \"0x...\" "
                "string, e.g. 65536.");
        else if (ctx.memory_size == 0)
            add("memory.size", "memory.size is 0.",
                "Set \"memory.size\" to at least 1 byte, e.g. 65536.");
        else if (ctx.addr_width_ok && ctx.addr_width < 128 &&
                 ctx.memory_size >
                     (static_cast<word_t>(1) << ctx.addr_width))
            add("memory.size",
                "memory.size is " +
                    word_to_dec_string(ctx.memory_size) +
                    " bytes, but a " +
                    std::to_string(ctx.addr_width) +
                    "-bit address bus can only address " +
                    word_to_dec_string(static_cast<word_t>(1)
                                       << ctx.addr_width) +
                    " bytes.",
                "Set \"memory.size\" to at most " +
                    word_to_dec_string(static_cast<word_t>(1)
                                       << ctx.addr_width) +
                    " (2 to the power of the address-bus width), or "
                    "widen \"address_bus.width\".");
        const std::string endianness =
            JsonString(doc["memory"], "endianness", "little");
        if (endianness != "little" && endianness != "big")
            add("memory.endianness",
                "memory.endianness is \"" + endianness + "\".",
                "Set \"endianness\" to \"little\" or \"big\".");
        const std::string arch =
            JsonString(doc["memory"], "architecture", "von_neumann");
        if (arch != "von_neumann" && arch != "harvard")
            add("memory.architecture",
                "memory.architecture is \"" + arch + "\".",
                "Set \"architecture\" to \"von_neumann\" or \"harvard\".");
    }
}

inline void CheckRegisters(const nlohmann::json &doc, const Context &ctx,
                           std::vector<Issue> &out) {
    auto add = [&](const std::string &loc, const std::string &problem,
                   const std::string &fix, int nav_index = -1,
                   const std::string &nav_name = "") {
        out.push_back({"Registers / " + loc, problem, fix, "Registers",
                       nav_index, nav_name});
    };
    if (!doc.contains("registers") || !doc["registers"].is_object()) {
        if (doc.contains("registers"))
            add("", "\"registers\" must be an object with "
                   "\"general_purpose\" and \"special\" arrays.",
                "Set \"registers\": { \"general_purpose\": [...], "
                "\"special\": [...] }.");
        return;
    }
    std::unordered_set<std::string> seen;
    std::unordered_map<std::string, int> counts;
    int pc_count = 0, sp_count = 0, flags_count = 0;
    std::function<void(const nlohmann::json &, const std::string &, int,
                       const std::string &, int, const std::string &)>
        walk = [&](const nlohmann::json &reg, const std::string &parent_role,
                   int parent_width, const std::string &parent_name,
                   int top_idx, const std::string &top_name) {
            if (!reg.is_object())
                return;
            // Issues on this register (or one of its sub-registers)
            // navigate to the top-level register that owns it.
            auto wadd = [&](const std::string &loc,
                            const std::string &problem,
                            const std::string &fix) {
                add(loc, problem, fix, top_idx, top_name);
            };
            const std::string name = JsonString(reg, "name");
            std::string loc;
            if (!reg.contains("name") || !reg["name"].is_string())
                loc = "(no name)";
            else if (name.empty())
                loc = "(empty name)";
            else {
                const int n = ++counts[name];
                loc = "'" + name + "'";
                if (n > 1)
                    loc += " (#" + std::to_string(n) + ")";
            }
            if (!reg.contains("name") || !reg["name"].is_string())
                wadd(loc, "A register has no \"name\".",
                    "Give the register a \"name\", e.g. \"R4\".");
            else if (name.empty())
                wadd(loc, "A register has an empty \"name\".",
                    "Give the register a non-empty, unique \"name\", "
                    "e.g. \"R4\".");
            else if (!seen.insert(name).second)
                wadd(loc,
                    "Register name '" + name + "' is used more than once.",
                    "Rename this register to a name no other register "
                    "(including sub-registers) uses.");
            const std::string role = JsonString(reg, "role", parent_role);
            if (role == "pc" || role == "program_counter")
                ++pc_count;
            else if (role == "sp" || role == "stack_pointer")
                ++sp_count;
            else if (role == "flags" || role == "status_flags")
                ++flags_count;
            if (!JsonHasInt(reg, "width"))
                wadd(loc, "Register '" + name + "' has no numeric \"width\".",
                    "Set \"width\" to a number from 1 to 128, e.g. 8.");
            const int width = JsonInt(reg, "width", 0);
            if (JsonHasInt(reg, "width") && (width < 1 || width > 128))
                wadd(loc,
                    "Register '" + name + "' has width " +
                        std::to_string(width) +
                        ", but width must be from 1 to 128.",
                    "Set \"width\" to a number from 1 to 128, e.g. 8.");
            if (reg.contains("initial")) {
                word_t initial = 0;
                if (!TryWord(reg["initial"], initial))
                    wadd(loc,
                        "Register '" + name + "' has an initial value (\"" +
                            ScalarText(reg["initial"]) +
                            "\") that cannot be parsed as a number.",
                        "Set \"initial\" to a decimal number or a "
                        "\"0x...\" string, e.g. 0.");
                else if (width >= 1 && width <= 128 &&
                         initial > mask_for_width(width))
                    wadd(loc,
                        "Register '" + name + "' has initial value " +
                            word_to_dec_string(initial) +
                            ", which does not fit in " +
                            std::to_string(width) +
                            " bits (max " +
                            word_to_dec_string(mask_for_width(width)) +
                            ").",
                        "Set \"initial\" to a value from 0 to " +
                            word_to_dec_string(mask_for_width(width)) +
                            ".");
            }
            const std::string own_role = JsonString(reg, "role");
            if (!own_role.empty() && !IsKnownRole(own_role))
                wadd(loc,
                    "Register '" + name + "' has unknown role \"" +
                        own_role + "\".",
                    "Set \"role\" to one of: pc, sp, flags, ir, "
                    "accumulator, program_counter, stack_pointer, "
                    "status_flags (or remove \"role\").");
            if (parent_width >= 0) {
                if (reg.contains("mask")) {
                    int active = 0;
                    if (!MaskActiveBits(reg["mask"], width, parent_width,
                                        active))
                        wadd(loc,
                            "Sub-register '" + name + "' has a mask (\"" +
                                ScalarText(reg["mask"]) +
                                "\") that cannot be parsed.",
                            "Set \"mask\" to a \"0b...\" or \"0x...\" "
                            "string, e.g. \"0b0101\".");
                    else if (active < width)
                        wadd(loc,
                            "Sub-register '" + name + "' is " +
                                std::to_string(width) +
                                " bits wide, but its mask only selects " +
                                std::to_string(active) +
                                " bit(s) of parent '" + parent_name + "'.",
                            "Widen the mask (add 1 bits) or narrow "
                            "\"width\" to " +
                                std::to_string(active) + ".");
                } else {
                    const int offset = JsonInt(reg, "offset", 0);
                    if (reg.contains("offset") &&
                        !JsonHasInt(reg, "offset"))
                        wadd(loc,
                            "Sub-register '" + name +
                                "' has a non-numeric \"offset\".",
                            "Set \"offset\" to a whole number, e.g. 0.");
                    else if (offset < 0)
                        wadd(loc,
                            "Sub-register '" + name + "' has a negative "
                            "offset (" +
                                std::to_string(offset) + ").",
                            "Set \"offset\" to a number from 0, e.g. 0.");
                    else if (offset + width > parent_width)
                        wadd(loc,
                            "Sub-register '" + name + "' (offset " +
                                std::to_string(offset) + ", width " +
                                std::to_string(width) +
                                ") reaches past its parent '" +
                                parent_name + "' (" +
                                std::to_string(parent_width) + " bits).",
                            "Set \"offset\" so that offset + width is at "
                            "most " +
                                std::to_string(parent_width) +
                                ", or narrow \"width\".");
                }
            }
            if (reg.contains("sub_registers") &&
                reg["sub_registers"].is_array())
                for (const auto &sub : reg["sub_registers"])
                    walk(sub, role, width, name, top_idx, top_name);
        };
    // Top-level registers in walk order (general_purpose first, then
    // special): the navigation index of a register or sub-register.
    int top_index = 0;
    for (const char *group : {"general_purpose", "special"}) {
        if (!doc["registers"].contains(group) ||
            !doc["registers"][group].is_array())
            continue;
        for (const auto &reg : doc["registers"][group])
            walk(reg, "", -1, "", top_index++, JsonString(reg, "name"));
    }
    (void)ctx;
    if (pc_count != 1)
        add("", "There are " + std::to_string(pc_count) +
                    " registers with the program_counter (pc) role; "
                    "exactly one is required.",
            "Give exactly one register (usually the PC) the role "
            "\"program_counter\" and remove the role from the others.");
    if (sp_count > 1)
        add("", "There are " + std::to_string(sp_count) +
                    " registers with the stack_pointer (sp) role; at most "
                    "one is allowed.",
            "Keep the role \"stack_pointer\" on one register (usually "
            "the SP) and remove it from the others.");
    if (flags_count > 1)
        add("", "There are " + std::to_string(flags_count) +
                    " registers with the status_flags (flags) role; at "
                    "most one is allowed.",
            "Keep the role \"status_flags\" on one register (usually "
            "the FLAGS register) and remove it from the others.");
}

inline void CheckAlu(const nlohmann::json &doc, const Context &ctx,
                     std::vector<Issue> &out) {
    auto add = [&](const std::string &loc, const std::string &problem,
                   const std::string &fix, int nav_index = -1,
                   const std::string &nav_name = "") {
        out.push_back({"ALU / " + loc, problem, fix, "ALU", nav_index,
                       nav_name});
    };
    if (!doc.contains("alu") || !doc["alu"].is_object())
        return;
    const auto &alu = doc["alu"];
    if (alu.contains("flags") && alu["flags"].is_array()) {
        if (!alu["flags"].empty() && ctx.flags_width == 0)
            add("flags",
                "Flags are defined, but no register has the "
                "flags/status_flags role, so no flag bit is valid.",
                "Give one register the role \"status_flags\" (usually a "
                "FLAGS register), e.g. { \"name\": \"FLAGS\", \"width\": "
                "8, \"role\": \"status_flags\" }.");
        std::unordered_set<std::string> seen_names;
        std::unordered_set<int> seen_bits;
        for (size_t fi = 0; fi < alu["flags"].size(); ++fi) {
            const auto &f = alu["flags"][fi];
            const std::string name = JsonString(f, "name");
            const std::string loc =
                "flag '" + (name.empty() ? std::string("?") : name) + "'";
            // Flag issues navigate to this flag in the ALU tab's flag
            // list (sel_flag); the UI tells flags apart from
            // operations by the "ALU / flag ..." prefix of `where`.
            auto fadd = [&](const std::string &l,
                            const std::string &problem,
                            const std::string &fix) {
                add(l, problem, fix, static_cast<int>(fi), name);
            };
            if (!f.contains("name") || !f["name"].is_string())
                fadd("flag (no name)", "A flag has no \"name\".",
                    "Give the flag a \"name\", e.g. \"Z\".");
            else if (name.empty())
                fadd(loc, "A flag has an empty \"name\".",
                    "Give the flag a non-empty \"name\", e.g. \"Z\".");
            else if (!seen_names.insert(name).second)
                fadd(loc, "Flag name '" + name + "' is used more than once.",
                    "Rename this flag to a name no other flag uses.");
            if (!JsonHasInt(f, "bit"))
                fadd(loc, "Flag '" + name + "' has no numeric \"bit\".",
                    "Set \"bit\" to a whole number from 0 to " +
                        std::to_string(ctx.flags_width - 1) + ".");
            else {
                const int bit = JsonInt(f, "bit", 0);
                if (ctx.flags_width > 0 &&
                    (bit < 0 || bit >= ctx.flags_width))
                    fadd(loc,
                        "Flag '" + name + "' uses bit " +
                            std::to_string(bit) +
                            ", but the FLAGS register is only " +
                            std::to_string(ctx.flags_width) +
                            " bits wide (bits 0.." +
                            std::to_string(ctx.flags_width - 1) + ").",
                        "Set \"bit\" to a number from 0 to " +
                            std::to_string(ctx.flags_width - 1) + ".");
                if (!seen_bits.insert(bit).second)
                    fadd(loc,
                        "Flag bit " + std::to_string(bit) +
                            " is used by more than one flag.",
                        "Give this flag a bit no other flag uses (0.." +
                            std::to_string(ctx.flags_width - 1) + ").");
            }
            const std::string expr = JsonString(f, "expression");
            if (!expr.empty() && !ValidFlagExpression(expr))
                fadd(loc,
                    "Flag '" + name + "' has an invalid expression \"" +
                        expr + "\".",
                    "Use only a, b, c, res and max_val in the "
                    "expression, e.g. \"res & 1\".");
        }
    }
    if (!alu.contains("operations") || !alu["operations"].is_array())
        return;
    std::unordered_set<std::string> seen_names;
    std::unordered_set<int> seen_codes;
    for (size_t oi = 0; oi < alu["operations"].size(); ++oi) {
        const auto &op = alu["operations"][oi];
        const std::string name = JsonString(op, "name");
        const std::string loc =
            "operation '" + (name.empty() ? std::string("?") : name) + "'";
        // Operation issues navigate to this operation in the ALU tab's
        // operations list (sel_alu_op).
        auto oadd = [&](const std::string &l, const std::string &problem,
                        const std::string &fix) {
            add(l, problem, fix, static_cast<int>(oi), name);
        };
        if (!op.contains("name") || !op["name"].is_string())
            oadd("operation (no name)", "An ALU operation has no \"name\".",
                "Give the operation a \"name\", e.g. \"ADD\".");
        else if (name.empty())
            oadd(loc, "An ALU operation has an empty \"name\".",
                "Give the operation a non-empty \"name\", e.g. \"ADD\".");
        else if (!seen_names.insert(name).second)
            oadd(loc,
                "ALU operation name '" + name + "' is used more than "
                "once.",
                "Rename this operation to a name no other ALU operation "
                "uses.");
        if (op.contains("code") && !JsonHasInt(op, "code"))
            oadd(loc,
                "ALU operation '" + name +
                    "' has a non-numeric \"code\".",
                "Set \"code\" to a whole number from 0 to 255.");
        else {
            // The loader defaults a missing code to 0, so two code-less
            // operations collide on code 0.
            const int code = JsonInt(op, "code", 0);
            if (code < 0 || code > 255)
                oadd(loc,
                    "ALU operation '" + name + "' has code " +
                        std::to_string(code) +
                        ", but codes must be from 0 to 255.",
                    "Set \"code\" to a number from 0 to 255.");
            else if (!seen_codes.insert(code).second)
                oadd(loc,
                    "ALU code " + std::to_string(code) +
                        " is used by more than one operation.",
                    "Give this operation a free code (0-255); the first "
                    "free code is " +
                        std::to_string(FirstFree(seen_codes, 255)) +
                        ". (The configurator assigns it automatically "
                        "for new operations.)");
        }
        if (op.contains("latency")) {
            if (!JsonHasInt(op, "latency"))
                oadd(loc,
                    "ALU operation '" + name +
                        "' has a non-numeric \"latency\".",
                    "Set \"latency\" to a whole number of at least 1.");
            else if (JsonInt(op, "latency", 1) < 1)
                oadd(loc,
                    "ALU operation '" + name + "' has latency " +
                        std::to_string(JsonInt(op, "latency", 1)) +
                        ", but latency must be at least 1 cycle.",
                    "Set \"latency\" to 1 or more.");
        }
        if (op.contains("program")) {
            std::vector<std::string> lines;
            bool ok_shape = true;
            if (op["program"].is_string())
                lines.push_back(op["program"].get<std::string>());
            else if (op["program"].is_array()) {
                for (const auto &line : op["program"]) {
                    if (!line.is_string()) {
                        ok_shape = false;
                        break;
                    }
                    lines.push_back(line.get<std::string>());
                }
            } else {
                ok_shape = false;
            }
            if (!ok_shape)
                oadd(loc,
                    "ALU operation '" + name +
                        "' has a \"program\" that is neither a string "
                        "nor an array of strings.",
                    "Set \"program\" to an array of source lines, e.g. "
                    "[\"acc = 0;\", \"return acc;\"].");
            else if (!ALU::is_valid_program(lines))
                oadd(loc,
                    "The program of ALU operation '" + name +
                        "' does not parse.",
                    "Check the mini-language: statements end with ';', "
                    "loops look like \"for i in 0..width { ... }\", "
                    "every block needs a matching '}', and the program "
                    "should end with \"return <expr>;\" (see "
                    "CONFIGURATION.md 4.4).");
        } else {
            const std::string expr = JsonString(op, "expression");
            if (expr.empty())
                oadd(loc,
                    "ALU operation '" + name +
                        "' has neither a valid \"expression\" nor a "
                        "\"program\".",
                    "Set \"expression\" to a valid expression using a, "
                    "b, c, e.g. \"a + b\", or use \"program\" instead.");
            else if (!ALU::is_valid_expression(expr))
                oadd(loc,
                    "ALU operation '" + name + "' has an invalid "
                    "expression \"" +
                        expr + "\".",
                    "Fix the expression: it must use a, b, c and "
                    "balanced parentheses, e.g. \"a + b\". (See "
                    "CONFIGURATION.md 4.3.)");
        }
    }
}

struct InstMeta {
    std::unordered_map<std::string, int> name_counts;
    std::unordered_map<int, int> code_counts;
};

inline InstMeta ComputeInstMeta(const nlohmann::json &doc) {
    InstMeta meta;
    if (!doc.contains("instruction_set") ||
        !doc["instruction_set"].is_object() ||
        !doc["instruction_set"].contains("instructions") ||
        !doc["instruction_set"]["instructions"].is_array())
        return meta;
    for (const auto &inst : doc["instruction_set"]["instructions"]) {
        if (!inst.is_object())
            continue;
        const std::string name = JsonString(inst, "name");
        if (!name.empty())
            ++meta.name_counts[name];
        if (JsonHasInt(inst, "opcode"))
            ++meta.code_counts[JsonInt(inst, "opcode", 0)];
    }
    return meta;
}

// Opcodes already used by the document's instructions.
inline std::unordered_set<int> UsedOpcodes(const nlohmann::json &doc) {
    std::unordered_set<int> used;
    if (!doc.contains("instruction_set") ||
        !doc["instruction_set"].is_object() ||
        !doc["instruction_set"].contains("instructions") ||
        !doc["instruction_set"]["instructions"].is_array())
        return used;
    for (const auto &inst : doc["instruction_set"]["instructions"])
        if (inst.is_object() && JsonHasInt(inst, "opcode"))
            used.insert(JsonInt(inst, "opcode", 0));
    return used;
}

// First free opcode, capped by the opcode_width mask when it is valid.
inline int FirstFreeOpcode(const nlohmann::json &doc, const Context &ctx) {
    const int max = (ctx.opcode_width >= 1 && ctx.opcode_width <= 16)
                        ? static_cast<int>(mask_for_width(ctx.opcode_width))
                        : 65535;
    return FirstFree(UsedOpcodes(doc), max);
}

// Example value to suggest when a required micro-op argument is missing.
inline std::string ExampleForArg(const std::string &key) {
    if (key == "out" || key == "dest")
        return "\"@dest\" (or \"$PC\")";
    if (key == "target")
        return "\"@address\"";
    if (key == "data" || key == "a" || key == "b" || key == "source" ||
        key == "addr")
        return "\"@src\" (or \"$PC\" / \"#0\")";
    if (key == "port" || key == "cp")
        return "\"#0\"";
    if (key == "reg")
        return "\"#0\"";
    return "\"@dest\"";
}

inline void CheckOperand(const Context &ctx,
                         const std::unordered_set<std::string> &decoded,
                         const std::string &where,
                         const std::string &action, int uop_index,
                         const std::string &arg_key,
                         const std::string &value, bool writable,
                         std::vector<Issue> &out, int nav_index,
                         const std::string &nav_name) {
    const std::string prefix = "Micro-op " + std::to_string(uop_index) +
                               " (" + action + ") argument '" + arg_key +
                               "': ";
    auto add = [&](const std::string &problem, const std::string &fix) {
        out.push_back({where, prefix + problem, fix, "Instructions",
                       nav_index, nav_name});
    };
    if (value.empty()) {
        add("is empty.",
            "Set \"" + arg_key + "\" to an operand reference, e.g. " +
                ExampleForArg(arg_key) + ".");
        return;
    }
    if (value[0] == '@') {
        const std::string name = value.substr(1);
        if (!decoded.count(name)) {
            std::string operands;
            for (const auto &op : decoded) {
                if (!operands.empty())
                    operands += ", ";
                operands += op;
            }
            add("'" + value +
                    "' is not an encoding operand of this instruction.",
                "Use one of this instruction's encoding operands: " +
                    (operands.empty() ? std::string("(none defined)")
                                     : operands) +
                    ", or add \"" + name +
                    "\" to the instruction's \"encoding\".");
        } else if (writable &&
                   name != "dest" && name != "src" &&
                   name != "addr_reg") {
            add("'" + value +
                    "' cannot be written to; only @dest, @src and "
                    "@addr_reg are writable targets.",
                "Change \"" + arg_key + "\" to \"@dest\" (or \"@src\" / "
                "\"@addr_reg\").");
        }
        return;
    }
    if (value[0] == '$') {
        const std::string name = value.substr(1);
        // The loader accepts $PC/$SP/$FLAGS always, $NEXT_PC only when
        // reading, and any configured register name.
        if (name == "PC" || name == "SP" || name == "FLAGS")
            return;
        if (name == "NEXT_PC") {
            if (writable)
                add("'" + value +
                        "' is read-only and cannot be written to.",
                    "Write to \"@dest\" or \"$PC\" instead.");
            return;
        }
        if (!ctx.reg_names.count(name)) {
            add("'" + value +
                    "' is not a register in this configuration.",
                "Use \"$PC\", \"$SP\", \"$FLAGS\", \"$NEXT_PC\", or one "
                "of the configured register names.");
        }
        return;
    }
    if (value[0] == '#') {
        word_t literal = 0;
        const std::string body = value.substr(1);
        if (body == "WORD_SIZE" || body == "ADDR_SIZE")
            return; // valid named literal
        if (writable) {
            add("literal '" + value + "' cannot be written to.",
                "Write to \"@dest\" or \"$PC\" instead.");
        } else if (!TryWord(nlohmann::json(body), literal)) {
            add("literal '" + value + "' cannot be parsed as a number.",
                "Use a decimal, \"0x...\" or \"0b...\" literal, or "
                "\"#WORD_SIZE\" / \"#ADDR_SIZE\".");
        }
        return;
    }
    if (writable)
        add("'" + value +
                "' is not a writable operand; operand references must "
                "start with @ or $.",
            "Use \"@dest\" for an instruction operand or \"$PC\" for a "
            "register.");
    else
        add("'" + value +
                "' is not an operand reference; it must start with @, $ "
                "or #.",
            "Use \"@dest\" for an instruction operand, \"$PC\" for a "
            "register, or \"#1\" for a literal.");
}

inline void DiagnoseOneInstruction(const nlohmann::json &doc,
                                   const Context &ctx,
                                   const nlohmann::json &inst, int index,
                                   const InstMeta &meta,
                                   std::vector<Issue> &out) {
    const std::string name = inst.is_object() ? JsonString(inst, "name")
                                              : std::string();
    std::string loc;
    if (!inst.is_object() || !inst.contains("name") ||
        !inst["name"].is_string())
        loc = "(no name)";
    else if (name.empty())
        loc = "(empty name)";
    else if (index >= 0)
        loc = "'" + name + "' (#" + std::to_string(index) + ")";
    else
        loc = "'" + name + "'";
    const std::string where = "Instructions / " + loc;
    // Instruction issues navigate to this instruction in the
    // Instructions tab's list (sel_instruction).
    auto add = [&](const std::string &problem, const std::string &fix) {
        out.push_back({where, problem, fix, "Instructions", index, name});
    };
    if (!inst.is_object()) {
        add("An instruction is not a JSON object.",
            "Replace it with an object like { \"name\": \"ADD\", "
            "\"opcode\": 0, \"encoding\": [0], \"microcode\": [...] }.");
        return;
    }
    if (!inst.contains("name") || !inst["name"].is_string())
        add("The instruction has no \"name\".",
            "Set \"name\" to a unique non-empty string, e.g. \"ADD\".");
    else if (name.empty())
        add("The instruction has an empty \"name\".",
            "Set \"name\" to a unique non-empty string, e.g. \"ADD\".");
    else if (meta.name_counts.count(name) && meta.name_counts.at(name) > 1)
        add("Instruction name '" + name + "' is used more than once.",
            "Rename this instruction to a name no other instruction "
            "uses.");
    const bool has_opcode = JsonHasInt(inst, "opcode");
    const int opcode = JsonInt(inst, "opcode", 0);
    const int max_opcode = (ctx.opcode_width >= 1 &&
                            ctx.opcode_width <= 16)
                               ? static_cast<int>(
                                     mask_for_width(ctx.opcode_width))
                               : 65535;
    if (!has_opcode)
        add("The instruction has no \"opcode\" (the loader requires "
            "one).",
            "Set \"opcode\" to a free number from 0 to " +
                std::to_string(max_opcode) +
                "; the first free opcode is " +
                std::to_string(FirstFreeOpcode(doc, ctx)) + ".");
    else {
        if (meta.code_counts.count(opcode) &&
            meta.code_counts.at(opcode) > 1)
            add("Opcode " + std::to_string(opcode) +
                    " is used by more than one instruction.",
                "Give this instruction a free opcode; the first free "
                "opcode is " +
                    std::to_string(FirstFreeOpcode(doc, ctx)) + ".");
        if (ctx.opcode_width >= 1 && ctx.opcode_width <= 16 &&
            static_cast<word_t>(opcode) > mask_for_width(ctx.opcode_width))
            add("Opcode " + std::to_string(opcode) +
                    " does not fit in opcode_width " +
                    std::to_string(ctx.opcode_width) + " bits (max " +
                    word_to_dec_string(mask_for_width(ctx.opcode_width)) +
                    ").",
                "Set \"opcode\" to at most " +
                    word_to_dec_string(mask_for_width(ctx.opcode_width)) +
                    ", or widen \"opcode_width\".");
    }

    std::unordered_set<std::string> decoded;
    if (!inst.contains("encoding") || !inst["encoding"].is_array() ||
        inst["encoding"].empty()) {
        add("The instruction has no \"encoding\" (it is missing or "
            "empty).",
            "Set \"encoding\" to an array whose first element is the "
            "opcode literal, e.g. \"encoding\": [" +
                std::to_string(opcode) + ", \"dest\", \"src\"].");
    } else {
        const auto &enc = inst["encoding"];
        int total_bits = 0;
        for (size_t i = 0; i < enc.size(); ++i) {
            const auto &tok = enc[i];
            if (tok.is_string()) {
                const std::string token = tok.get<std::string>();
                const int code = EncodingTokenCode(token);
                if (code == -10) {
                    add("Encoding element \"" + token +
                            "\" is not a known encoding element.",
                        "Use one of: \"dest\", \"src\", \"addr_reg\", "
                        "\"offset\", \"imm8\", \"imm16\", \"address\", "
                        "\"imm\", or a number literal.");
                    continue;
                }
                // The loader maps "imm" to the same field as "imm8",
                // so @imm is not a valid reference; only the canonical
                // names below are.
                decoded.insert(code == -1   ? "dest"
                               : code == -2 ? "src"
                               : code == -3 ? "addr_reg"
                               : code == -4 ? "offset"
                               : code == -5 ? "imm8"
                               : code == -6 ? "imm16"
                                            : "address");
                total_bits +=
                    (code == -1 || code == -2 || code == -3)
                        ? ctx.reg_bits
                        : (code == -4 || code == -5) ? 8
                        : code == -6              ? 16
                                                  : ctx.addr_width;
            } else if (tok.is_number_integer() ||
                       tok.is_number_unsigned()) {
                int v = 0x7fffffff; // absurd literal if out of int range
                try {
                    v = tok.is_number_integer()
                            ? tok.get<int>()
                            : static_cast<int>(tok.get<uint64_t>());
                } catch (...) {
                }
                if (i == 0) {
                    total_bits += ctx.opcode_width;
                    if (has_opcode && v != opcode)
                        add("encoding[0] is " + std::to_string(v) +
                                ", but the opcode is " +
                                std::to_string(opcode) +
                                "; the decoder uses encoding[0] as the "
                                "opcode, so this instruction would never "
                                "decode.",
                            "Set \"encoding\": [" +
                                std::to_string(opcode) +
                                ", ...] so the first element equals "
                                "\"opcode\". (The configurator keeps them "
                                "in sync automatically.)");
                } else {
                    total_bits += 4;
                    if (v < 0 || v > 15)
                        add("Encoding literal " + std::to_string(v) +
                                " at position " +
                                std::to_string(i) +
                                " does not fit in its 4-bit field (0..15).",
                            "Set the literal at position " +
                                std::to_string(i) +
                                " to a number from 0 to 15, or use a "
                                "named operand (\"imm8\", \"imm16\", "
                                "\"address\") instead.");
                }
            } else {
                add("Encoding element at position " + std::to_string(i) +
                        " is not a string or a number.",
                    "Use a named operand (\"dest\", \"src\", ...) or a "
                    "number literal in \"encoding\".");
                total_bits += 4;
            }
        }
        if (total_bits > 128)
            add("The encoding fetches " + std::to_string(total_bits) +
                    " bits, but at most 128 bits may be fetched.",
                "Shorten the \"encoding\" (remove operands or use "
                "narrower fields) so it totals at most 128 bits.");
    }

    if (!inst.contains("microcode") || !inst["microcode"].is_array() ||
        inst["microcode"].empty()) {
        add("The instruction has no microcode (it is missing or empty).",
            "Add at least one micro-op to \"microcode\", e.g. "
            "{ \"action\": \"alu\", \"op\": \"ADD\", \"a\": "
            "\"@src\", \"out\": \"@dest\" } (arguments are flat keys on "
            "the micro-op, not an \"args\" object).");
        return;
    }
    static const std::unordered_map<std::string,
                                     std::vector<std::string>>
        kRequiredArgs = {
            {"copy", {"source", "dest"}},
            {"alu", {"op", "a", "out"}},
            {"mem_read", {"addr", "out"}},
            {"mem_write", {"addr", "data"}},
            {"port_read", {"port", "out"}},
            {"port_write", {"port", "data"}},
            {"coproc_read", {"cp", "reg", "out"}},
            {"coproc_write", {"cp", "reg", "data"}},
            {"branch", {"target"}},
        };
    static const std::unordered_set<std::string> kOperandKeys = {
        "a", "b", "c", "out", "dest", "source", "addr", "data", "reg",
        "cp", "port", "target"};
    static const std::unordered_set<std::string> kWritableKeys = {
        "out", "dest"};
    static const std::unordered_set<std::string> kKnownActions = {
        "copy", "alu", "mem_read", "mem_write", "port_read", "port_write",
        "coproc_read", "coproc_write", "branch", "halt"};
    // 1-based: the preview table numbers micro-ops from 1, so the
    // Problems list must say "Micro-op 1" for the first one, not
    // "Micro-op 0".
    int uop_index = 1;
    for (const auto &uop : inst["microcode"]) {
        if (!uop.is_object()) {
            add("Micro-op " + std::to_string(uop_index) +
                    " is not a JSON object.",
                "Replace it with an object like { \"action\": \"alu\", "
                "\"args\": { ... } }.");
            ++uop_index;
            continue;
        }
        const std::string action = JsonString(uop, "action");
        if (action.empty()) {
            add("Micro-op " + std::to_string(uop_index) +
                    " has no \"action\".",
                "Set \"action\" to one of: copy, alu, mem_read, "
                "mem_write, port_read, port_write, coproc_read, "
                "coproc_write, branch, halt.");
            ++uop_index;
            continue;
        }
        if (!kKnownActions.count(action)) {
            add("Micro-op " + std::to_string(uop_index) +
                    " has unknown action \"" + action + "\".",
                "Set \"action\" to one of: copy, alu, mem_read, "
                "mem_write, port_read, port_write, coproc_read, "
                "coproc_write, branch, halt.");
            ++uop_index;
            continue;
        }
        // Micro-op arguments are flat keys on the micro-op object (all
        // keys except "action"), not an "args" sub-object.
        nlohmann::json args = nlohmann::json::object();
        for (auto it = uop.begin(); it != uop.end(); ++it)
            if (it.key() != "action")
                args[it.key()] = it.value();
        auto has_arg = [&](const std::string &key) {
            return args.contains(key) && !args[key].is_null() &&
                   !ScalarText(args[key]).empty();
        };
        const auto req_it = kRequiredArgs.find(action);
        if (req_it != kRequiredArgs.end())
            for (const auto &key : req_it->second)
                if (!has_arg(key))
                    add("Micro-op " + std::to_string(uop_index) + " (" +
                            action + ") is missing the required argument " +
                            "\"" + key + "\".",
                        "Add \"" + key + "\": " + ExampleForArg(key) +
                            " to this micro-op.");
        if (action == "alu" && has_arg("op")) {
            const std::string op_name = ScalarText(args["op"]);
            if (!ctx.alu_names.count(op_name)) {
                std::string known;
                for (const auto &n : ctx.alu_names) {
                    if (!known.empty())
                        known += ", ";
                    known += n;
                }
                add("Micro-op " + std::to_string(uop_index) +
                        " (alu) uses ALU operation \"" + op_name +
                        "\", which is not defined in alu.operations.",
                    "Set \"op\" to one of the defined ALU operations: " +
                        (known.empty() ? std::string("(none defined)")
                                       : known) +
                        ", or add \"" + op_name +
                        "\" to \"alu\": { \"operations\": [...] }.");
            }
        }
        if (action == "branch" && args.contains("condition") &&
            !args["condition"].is_null()) {
            const std::string raw = ScalarText(args["condition"]);
            std::string cond = raw;
            if (!cond.empty() && cond[0] == '!')
                cond = cond.substr(1);
            if (cond.empty() || (!ctx.flag_names.count(cond) &&
                                 !ctx.flag_types.count(cond)))
                add("Micro-op " + std::to_string(uop_index) +
                        " (branch) has condition \"" + raw +
                        "\", which is not a defined flag name or type.",
                    "Set \"condition\" to a flag name from "
                    "alu.flags (e.g. \"Z\") or a flag type (e.g. "
                    "\"zero\"), optionally prefixed with \"!\", e.g. "
                    "\"!Z\".");
        }
        for (auto it = args.begin(); it != args.end(); ++it) {
            const std::string key = it.key();
            if (!kOperandKeys.count(key))
                continue;
            CheckOperand(ctx, decoded, where, action, uop_index, key,
                         ScalarText(it.value()),
                         kWritableKeys.count(key) != 0, out, index, name);
        }
        ++uop_index;
    }
}

inline void CheckMemory(const nlohmann::json &doc, const Context &ctx,
                        std::vector<Issue> &out) {
    auto add = [&](const std::string &loc, const std::string &problem,
                   const std::string &fix, int nav_index = -1,
                   const std::string &nav_name = "") {
        out.push_back({"Memory / " + loc, problem, fix, "Memory",
                       nav_index, nav_name});
    };
    if (!doc.contains("memory") || !doc["memory"].is_object() ||
        !doc["memory"].contains("segments") ||
        !doc["memory"]["segments"].is_array())
        return;
    struct Seg {
        std::string name;
        word_t start = 0, end = 0;
        bool ok = false;
        std::string loc;
    };
    std::vector<Seg> segs;
    int idx = 0;
    for (const auto &s : doc["memory"]["segments"]) {
        Seg seg;
        seg.name = s.is_object() ? JsonString(s, "name") : std::string();
        seg.loc = seg.name.empty()
                      ? "segment #" + std::to_string(idx) + " (no name)"
                      : "segment '" + seg.name + "'";
        // Segment issues navigate to this segment in the Memory tab's
        // list (sel_segment).
        auto sadd = [&](const std::string &l, const std::string &problem,
                        const std::string &fix) {
            add(l, problem, fix, idx, seg.name);
        };
        if (s.is_object() && s.contains("start") && s.contains("end") &&
            TryWord(s["start"], seg.start) &&
            TryWord(s["end"], seg.end))
            seg.ok = true;
        else
            sadd(seg.loc,
                "Segment '" + seg.name +
                    "' has a missing or unparsable \"start\" or \"end\".",
                "Set \"start\" and \"end\" to addresses, e.g. "
                "\"start\": 0, \"end\": 8191.");
        if (seg.ok) {
            if (seg.start > seg.end)
                sadd(seg.loc,
                    "Segment '" + seg.name + "' starts at " +
                        word_to_dec_string(seg.start) + " but ends at " +
                        word_to_dec_string(seg.end) +
                        " (start is after end).",
                    "Set \"start\" to an address at or below \"end\", "
                    "e.g. \"start\": 0, \"end\": " +
                        word_to_dec_string(seg.end) + ".");
            else if (ctx.memory_size_ok && seg.end >= ctx.memory_size)
                sadd(seg.loc,
                    "Segment '" + seg.name + "' ends at " +
                        word_to_dec_string(seg.end) +
                        ", but memory.size is only " +
                        word_to_dec_string(ctx.memory_size) +
                        " bytes (last valid address " +
                        word_to_dec_string(ctx.memory_size - 1) + ").",
                    "Set \"end\" to at most " +
                        word_to_dec_string(ctx.memory_size - 1) +
                        ", or increase \"memory.size\".");
        }
        segs.push_back(seg);
        ++idx;
    }
    for (size_t i = 0; i < segs.size(); ++i)
        for (size_t j = i + 1; j < segs.size(); ++j) {
            if (!segs[i].ok || !segs[j].ok)
                continue;
            if (segs[i].start <= segs[j].end &&
                segs[j].start <= segs[i].end)
                // An overlap belongs to both segments; navigate to the
                // first one.
                add(segs[i].loc + " / " + segs[j].loc,
                    "Segments '" + segs[i].name + "' and '" +
                        segs[j].name + "' overlap (" +
                        word_to_dec_string(segs[i].start) + "-" +
                        word_to_dec_string(segs[i].end) + " vs " +
                        word_to_dec_string(segs[j].start) + "-" +
                        word_to_dec_string(segs[j].end) + ").",
                    "Move one of the segments so their ranges no longer "
                    "overlap, e.g. set the second segment's \"start\" "
                    "above " +
                        word_to_dec_string(segs[i].end) + ".",
                    static_cast<int>(i), segs[i].name);
        }
}

inline void CheckPeripherals(const nlohmann::json &doc, const Context &ctx,
                             std::vector<Issue> &out) {
    auto add = [&](const std::string &loc, const std::string &problem,
                   const std::string &fix, int nav_index = -1,
                   const std::string &nav_name = "") {
        out.push_back({"Peripherals / " + loc, problem, fix, "Peripherals",
                       nav_index, nav_name});
    };
    if (!doc.contains("peripherals") || !doc["peripherals"].is_array())
        return;
    struct Range {
        std::string name, type;
        word_t start = 0, end = 0;
        bool ok = false;
        std::string loc;
    };
    std::vector<Range> ranges;
    std::unordered_set<std::string> seen_names;
    int idx = 0;
    for (const auto &p : doc["peripherals"]) {
        const std::string name = p.is_object() ? JsonString(p, "name")
                                               : std::string();
        const std::string loc =
            name.empty() ? "peripheral #" + std::to_string(idx) +
                               " (no name)"
                         : "'" + name + "'";
        // Peripheral issues (including ones about this peripheral's
        // registers) navigate to this peripheral in the Peripherals
        // tab's list (sel_peripheral).
        auto padd = [&](const std::string &l, const std::string &problem,
                        const std::string &fix) {
            add(l, problem, fix, idx, name);
        };
        if (!p.is_object()) {
            padd(loc, "A peripheral is not a JSON object.",
                "Replace it with an object like { \"name\": \"Serial\", "
                "\"type\": \"text_display\", \"address_start\": 0, "
                "\"address_end\": 255 }.");
            ++idx;
            continue;
        }
        if (!p.contains("name") || !p["name"].is_string())
            padd(loc, "A peripheral has no \"name\".",
                "Give the peripheral a \"name\", e.g. \"Serial\".");
        else if (name.empty())
            padd(loc, "A peripheral has an empty \"name\".",
                "Give the peripheral a non-empty \"name\", e.g. "
                "\"Serial\".");
        else if (!seen_names.insert(name).second)
            padd(loc,
                "Peripheral name '" + name + "' is used more than once.",
                "Rename this peripheral to a name no other peripheral "
                "uses.");
        const std::string type = JsonString(p, "type");
        if (type != "text_display" && type != "grid_display" &&
            type != "input" && type != "declarative")
            padd(loc,
                "Peripheral '" + name + "' has unknown type \"" + type +
                    "\".",
                "Set \"type\" to one of: text_display, grid_display, "
                "input, declarative.");
        if (type == "grid_display" && p.contains("parameters") &&
            p["parameters"].is_object() &&
            p["parameters"].contains("width")) {
            // The loader reads the grid width from parameters.width
            // (a string or number) and rejects non-positive or
            // non-numeric values; an absent width is not checked.
            const std::string width_text =
                ScalarText(p["parameters"]["width"]);
            try {
                const int w = std::stoi(width_text);
                if (w <= 0)
                    padd(loc,
                        "Peripheral '" + name + "' has grid width " +
                            std::to_string(w) +
                            ", but width must be at least 1.",
                        "Set \"parameters\": { \"width\": \"16\" } (or a "
                        "number) with a value of at least 1.");
            } catch (...) {
                padd(loc,
                    "Peripheral '" + name +
                        "' has a non-numeric grid \"width\" (\"" +
                        width_text + "\").",
                    "Set \"parameters\": { \"width\": \"16\" } to a whole "
                    "number, e.g. \"16\".");
            }
        }
        Range range;
        range.name = name;
        range.type = type;
        range.loc = loc;
        word_t start = 0, end = 0;
        if (p.contains("address_start") && p.contains("address_end") &&
            TryWord(p["address_start"], start) &&
            TryWord(p["address_end"], end)) {
            range.start = start;
            range.end = end;
            range.ok = true;
            if (start > end)
                padd(loc,
                    "Peripheral '" + name + "' starts at " +
                        word_to_dec_string(start) + " but ends at " +
                        word_to_dec_string(end) +
                        " (start is after end).",
                    "Set \"address_start\" to an address at or below "
                    "\"address_end\", e.g. \"address_start\": 0.");
            else if (ctx.memory_size_ok && end >= ctx.memory_size)
                padd(loc,
                    "Peripheral '" + name + "' ends at " +
                        word_to_dec_string(end) +
                        ", but memory.size is only " +
                        word_to_dec_string(ctx.memory_size) +
                        " bytes (last valid address " +
                        word_to_dec_string(ctx.memory_size - 1) + ").",
                    "Set \"address_end\" to at most " +
                        word_to_dec_string(ctx.memory_size - 1) +
                        ", or increase \"memory.size\".");
        } else {
            padd(loc,
                "Peripheral '" + name +
                    "' has a missing or unparsable \"address_start\" or "
                    "\"address_end\".",
                "Set \"address_start\" and \"address_end\" to addresses, "
                "e.g. \"address_start\": 0, \"address_end\": 255.");
        }
        if (p.contains("registers") && p["registers"].is_array()) {
            const word_t region =
                range.ok ? (range.end - range.start + 1) : 0;
            std::unordered_set<std::string> seen_reg_names;
            for (const auto &r : p["registers"]) {
                const std::string rname =
                    r.is_object() ? JsonString(r, "name") : std::string();
                const std::string rloc =
                    loc + " register '" +
                    (rname.empty() ? std::string("?") : rname) + "'";
                if (!r.is_object())
                    continue;
                if (!rname.empty() && !seen_reg_names.insert(rname).second)
                    padd(rloc,
                        "Peripheral register '" + rname +
                            "' is defined more than once in peripheral '" +
                            name + "'.",
                        "Rename this register to a name no other register "
                        "of this peripheral uses.");
                if (!JsonHasInt(r, "offset"))
                    padd(rloc,
                        "Peripheral register '" + rname +
                            "' has no numeric \"offset\".",
                        "Set \"offset\" to a whole number, e.g. "
                        "\"offset\": 0.");
                else if (JsonInt(r, "offset", 0) < 0)
                    padd(rloc,
                        "Peripheral register '" + rname +
                            "' has a negative \"offset\".",
                        "Set \"offset\" to a number from 0.");
                // The loader defaults a missing size_bytes to 1.
                const int size = JsonInt(r, "size_bytes", 1);
                if (r.contains("size_bytes") && !JsonHasInt(r, "size_bytes"))
                    padd(rloc,
                        "Peripheral register '" + rname +
                            "' has a non-numeric \"size_bytes\".",
                        "Set \"size_bytes\" to a whole number from 1 to "
                        "16, e.g. \"size_bytes\": 1.");
                else if (size < 1 || size > 16)
                    padd(rloc,
                        "Peripheral register '" + rname + "' has "
                        "size_bytes " +
                            std::to_string(size) +
                            ", but it must be from 1 to 16.",
                        "Set \"size_bytes\" to a number from 1 to 16, "
                        "e.g. \"size_bytes\": 1.");
                if (range.ok && JsonHasInt(r, "offset") &&
                    JsonInt(r, "offset", 0) >= 0 && size >= 1 &&
                    static_cast<word_t>(JsonInt(r, "offset", 0) + size) >
                        region)
                    padd(rloc,
                        "Peripheral register '" + rname +
                            "' (offset " +
                            std::to_string(JsonInt(r, "offset", 0)) +
                            ", size_bytes " + std::to_string(size) +
                            ") reaches past the end of peripheral '" +
                            name + "' (" +
                            word_to_dec_string(region) + " bytes).",
                        "Set \"offset\" and \"size_bytes\" so offset + "
                        "size_bytes is at most " +
                            word_to_dec_string(region) + ".");
                const std::string access = JsonString(r, "access", "rw");
                if (access != "r" && access != "w" && access != "rw")
                    padd(rloc,
                        "Peripheral register '" + rname +
                            "' has access \"" + access + "\".",
                        "Set \"access\" to \"r\", \"w\" or \"rw\".");
            }
        }
        ranges.push_back(range);
        ++idx;
    }
    for (size_t i = 0; i < ranges.size(); ++i)
        for (size_t j = i + 1; j < ranges.size(); ++j) {
            if (!ranges[i].ok || !ranges[j].ok)
                continue;
            // The layered terminal: a text_display and a declarative
            // peripheral may share one address range on purpose.
            const bool layered =
                (ranges[i].type == "text_display" &&
                 ranges[j].type == "declarative") ||
                (ranges[i].type == "declarative" &&
                 ranges[j].type == "text_display");
            if (layered)
                continue;
            if (ranges[i].start <= ranges[j].end &&
                ranges[j].start <= ranges[i].end)
                // An overlap belongs to both peripherals; navigate to
                // the first one.
                add(ranges[i].loc + " / " + ranges[j].loc,
                    "Peripherals '" + ranges[i].name + "' and '" +
                        ranges[j].name + "' overlap (" +
                        word_to_dec_string(ranges[i].start) + "-" +
                        word_to_dec_string(ranges[i].end) + " vs " +
                        word_to_dec_string(ranges[j].start) + "-" +
                        word_to_dec_string(ranges[j].end) + ").",
                    "Move one of the peripherals so their address "
                    "ranges no longer overlap.",
                    static_cast<int>(i), ranges[i].name);
        }
}

} // namespace Detail

// Every problem in the document, in area order: System, Registers, ALU,
// Instructions, Memory, Peripherals.
inline std::vector<Issue> Diagnose(const nlohmann::json &doc) {
    std::vector<Issue> out;
    if (!doc.is_object()) {
        out.push_back(
            {"System",
             "The configuration document is not a JSON object.",
             "Load a JSON object with \"name\", \"data_bus\", "
             "\"address_bus\", \"memory\", \"registers\", \"alu\" and "
             "\"instruction_set\" (see CONFIGURATION.md).",
             "System", -1, ""});
        return out;
    }
    const Context ctx = BuildContext(doc);
    Detail::CheckSystem(doc, ctx, out);
    Detail::CheckRegisters(doc, ctx, out);
    Detail::CheckAlu(doc, ctx, out);
    if (!doc.contains("instruction_set") ||
        !doc["instruction_set"].is_object()) {
        out.push_back({"Instructions",
                       "The \"instruction_set\" object is missing.",
                       "Add \"instruction_set\": { \"opcode_width\": 8, "
                       "\"instructions\": [...] }.",
                       "Instructions", -1, ""});
    } else if (!doc["instruction_set"].contains("instructions") ||
               !doc["instruction_set"]["instructions"].is_array()) {
        out.push_back({"Instructions",
                       "\"instruction_set.instructions\" is missing or "
                       "not an array.",
                       "Set \"instructions\" to an array of instruction "
                       "objects, e.g. [{ \"name\": \"ADD\", \"opcode\": "
                       "0, \"encoding\": [0], \"microcode\": [...] }].",
                       "Instructions", -1, ""});
    } else {
        const Detail::InstMeta meta = Detail::ComputeInstMeta(doc);
        int index = 0;
        for (const auto &inst :
             doc["instruction_set"]["instructions"]) {
            Detail::DiagnoseOneInstruction(doc, ctx, inst, index, meta,
                                           out);
            ++index;
        }
    }
    Detail::CheckMemory(doc, ctx, out);
    Detail::CheckPeripherals(doc, ctx, out);
    return out;
}

// Only the problems of one instruction (used under the instruction
// editor). The instruction is matched by value inside the document's
// instruction list, so duplicates are counted against the whole set.
inline std::vector<Issue> DiagnoseInstruction(const nlohmann::json &doc,
                                              const nlohmann::json &inst) {
    const Context ctx = BuildContext(doc);
    const Detail::InstMeta meta = Detail::ComputeInstMeta(doc);
    int index = -1;
    if (doc.contains("instruction_set") &&
        doc["instruction_set"].is_object() &&
        doc["instruction_set"].contains("instructions") &&
        doc["instruction_set"]["instructions"].is_array()) {
        const auto &list = doc["instruction_set"]["instructions"];
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i] == inst) {
                index = static_cast<int>(i);
                break;
            }
    }
    std::vector<Issue> out;
    Detail::DiagnoseOneInstruction(doc, ctx, inst, index, meta, out);
    return out;
}

} // namespace ConfigDiagnostics
