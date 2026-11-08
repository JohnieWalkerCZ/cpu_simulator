#pragma once
// Graphical CPU configurator: edits a CPU architecture as an in-memory
// nlohmann::json document (not the flattened Config structs), so a loaded
// file round-trips faithfully -- nested sub-registers, hex strings such as
// "0x2000" and raw peripheral JSON are preserved as written. The document
// is validated live with Config::from_json() + Config::validate(); Export
// and Launch are blocked while it is invalid. On top of the loader's
// pass/fail verdict, ConfigDiagnostics::Diagnose() lists every problem
// with a concrete fix: per instruction under its editor, and all of them
// grouped by area in the Problems tab.
//
// Layout: top bar (file path, presets, Save / Save As / Export / Launch,
// validation status) plus tabs for System, Registers, Memory, ALU,
// Instructions, Problems, Peripherals and a raw JSON view.

#include "app_launcher.hpp"
#include "config_diagnostics.hpp"
#include "core/config.hpp"
#include "hook_editor.hpp"
#include "instruction_preview.hpp"
#include "microop_editor.hpp"
#include "register_layout.hpp"
#include "ui_common.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ConfiguratorDetail {

inline std::string Trim(const std::string &s) {
    size_t begin = 0;
    while (begin < s.size() &&
           std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(begin, end - begin);
}

inline std::vector<std::string> SplitLines(const std::string &text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    lines.push_back(current);
    while (!lines.empty() && Trim(lines.back()).empty())
        lines.pop_back();
    return lines;
}

inline std::string JoinLines(const std::vector<std::string> &lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0)
            out += '\n';
        out += lines[i];
    }
    return out;
}

// Display form of a JSON scalar: strings are shown raw (so "0x2000" stays
// "0x2000"), numbers/booleans are dumped.
inline std::string JsonScalarToString(const nlohmann::json &v) {
    if (v.is_string())
        return v.get<std::string>();
    return v.dump();
}

// Smallest non-negative integer not used as `key` in any object of `arr`,
// capped at `max_value`. Returns -1 when every value in [0, max_value] is
// taken. Used so a newly added ALU operation or instruction gets the first
// free code/opcode instead of colliding with an existing one (code 0 is
// only free when nothing uses it yet).
inline int FirstFreeCode(const nlohmann::json &arr, const char *key,
                         int max_value) {
    if (!arr.is_array())
        return 0;
    std::vector<bool> used(static_cast<size_t>(max_value) + 1, false);
    for (const auto &item : arr) {
        if (item.is_object() && item.contains(key) &&
            item[key].is_number_integer()) {
            const int v = item[key].get<int>();
            if (v >= 0 && v <= max_value)
                used[static_cast<size_t>(v)] = true;
        }
    }
    for (int v = 0; v <= max_value; ++v)
        if (!used[static_cast<size_t>(v)])
            return v;
    return -1;
}

// Keep an instruction's encoding in sync with its opcode: the first
// encoding element is the opcode literal (it occupies opcode_width bits
// in the decoder layout), so it must equal `opcode`. A missing or empty
// encoding becomes [opcode]; otherwise its first element is replaced.
inline void SyncEncodingWithOpcode(nlohmann::json &inst, int opcode) {
    if (!inst.contains("encoding") || !inst["encoding"].is_array() ||
        inst["encoding"].empty())
        inst["encoding"] = nlohmann::json::array({opcode});
    else
        inst["encoding"][0] = opcode;
}

// Commit an edited word-sized value (register initial, segment start/end,
// peripheral address). Hex/binary strings stay strings, plain integers
// become JSON numbers, anything else is kept as a string (the config
// parser accepts decimal strings too).
inline void CommitWordText(nlohmann::json &slot, const std::string &text) {
    const std::string t = Trim(text);
    if (t.empty())
        return;
    if (t.size() > 2 && (t.compare(0, 2, "0x") == 0 ||
                         t.compare(0, 2, "0X") == 0 ||
                         t.compare(0, 2, "0b") == 0 ||
                         t.compare(0, 2, "0B") == 0)) {
        slot = t;
        return;
    }
    try {
        size_t used = 0;
        const long long v = std::stoll(t, &used);
        if (used == t.size() && v >= 0) {
            slot = v;
            return;
        }
    } catch (...) {
    }
    slot = t;
}

inline int TextResizeCallback(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto *str = static_cast<std::string *>(data->UserData);
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

// Parse "key = value" (or "key: value") lines into pairs. Returns false
// with `error` set when a non-empty line has no separator.
inline bool ParseKeyValueLines(
    const std::string &text,
    std::vector<std::pair<std::string, std::string>> &out,
    std::string &error) {
    out.clear();
    for (const std::string &raw : SplitLines(text)) {
        const std::string line = Trim(raw);
        if (line.empty() || line.rfind("//", 0) == 0)
            continue;
        const size_t sep = line.find_first_of("=:");
        if (sep == std::string::npos) {
            error = "Line has no '=' or ':': " + line;
            return false;
        }
        const std::string key = Trim(line.substr(0, sep));
        const std::string value = Trim(line.substr(sep + 1));
        if (key.empty()) {
            error = "Empty key in line: " + line;
            return false;
        }
        out.emplace_back(key, value);
    }
    return true;
}

inline std::string KeyValueMapToText(const nlohmann::json &obj) {
    std::string out;
    if (!obj.is_object())
        return out;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!out.empty())
            out += '\n';
        out += it.key();
        out += " = ";
        out += JsonScalarToString(it.value());
    }
    return out;
}

// A valid starting point when the configurator is opened without a file:
// the smallest architecture that passes Config::validate().
inline nlohmann::json DefaultTemplate() {
    return nlohmann::json::parse(R"({
        "name": "NewCPU",
        "data_bus": { "width": 8 },
        "address_bus": { "width": 16 },
        "memory": {
            "size": 65536,
            "architecture": "von_neumann",
            "endianness": "little",
            "segments": [
                { "name": "RAM", "start": "0x0000", "end": "0xFFFF",
                  "R": true, "W": true, "X": true }
            ]
        },
        "registers": {
            "general_purpose": [
                { "name": "R0", "width": 8, "initial": 0 }
            ],
            "special": [
                { "name": "PC", "width": 16, "initial": 0,
                  "role": "program_counter" },
                { "name": "FLAGS", "width": 8, "initial": 0,
                  "role": "status_flags" }
            ]
        },
        "alu": {
            "flags": [
                { "name": "Z", "bit": 0, "type": "zero" }
            ],
            "operations": [
                { "name": "ADD", "code": 1, "expression": "a + b",
                  "flag_rules": { "Z": "result_zero" }, "latency": 1 }
            ]
        },
        "instruction_set": {
            "instructions": [
                { "name": "HLT", "opcode": 15, "encoding": [15],
                  "microcode": [ { "action": "halt" } ] }
            ]
        },
        "peripherals": []
    })");
}

inline std::string ResolvePresetPath(const std::string &file) {
    for (const char *base : {"configs/", "../configs/", "../../configs/"}) {
        const std::string candidate = std::string(base) + file;
        std::ifstream f(candidate);
        if (f.good())
            return candidate;
    }
    return "configs/" + file;
}

} // namespace ConfiguratorDetail

struct ConfiguratorState {
    nlohmann::json doc = ConfiguratorDetail::DefaultTemplate();
    std::string path;
    std::string argv0;
    bool dirty = false;
    bool valid = false;
    std::string validation_msg = "Not validated yet.";
    int problem_count = 0; // issues found by ConfigDiagnostics::Diagnose
    std::vector<ConfigDiagnostics::Issue> problems; // same list, cached
    std::string action_msg;
    std::string launch_message;
    UITheme theme = UITheme::MODERN_DARK;

    int doc_version = 0;
    int last_validated_version = -1;

    // Text buffers keyed by a stable id (e.g. "reg:gp/0/name"). A buffer is
    // re-synced from the document whenever its source value changed and the
    // widget is not being edited; buffers flagged in no_resync keep
    // user-typed text that failed to parse, so it is not lost.
    std::unordered_map<std::string, std::string> text_bufs;
    std::unordered_set<std::string> text_no_resync;
    std::string active_text_key;

    // List selections.
    int sel_group = 0; // 0 = general_purpose, 1 = special
    int sel_register = 0;
    int sel_segment = 0;
    int sel_flag = 0;
    int sel_alu_op = 0;
    int sel_instruction = 0;
    int sel_microop = 0;
    int sel_peripheral = 0;
    int sel_periph_reg = 0;

    // Hook ids shown in text mode; the rest use the graphical blocks
    // view (default). A hook id is the peripheral/register path of the
    // hook ("per/0/tick", "per/1/preg/0/on_write"). Indices shift when
    // a peripheral or register is removed, which only ever flips a view
    // back to blocks.
    std::unordered_set<std::string> hook_text_view;

    void MarkDirty() {
        dirty = true;
        ++doc_version;
    }

    void ClearTextBufs() {
        text_bufs.clear();
        text_no_resync.clear();
        active_text_key.clear();
    }

    void LoadTemplate() {
        doc = ConfiguratorDetail::DefaultTemplate();
        path.clear();
        dirty = false;
        ++doc_version;
        ClearTextBufs();
        action_msg = "Started with a blank template.";
    }

    bool LoadFromFile(const std::string &file_path) {
        std::ifstream f(file_path);
        if (!f.is_open()) {
            action_msg = "Cannot open file: " + file_path;
            return false;
        }
        nlohmann::json parsed;
        try {
            f >> parsed;
        } catch (const std::exception &e) {
            action_msg = std::string("Cannot parse JSON in ") + file_path +
                         ": " + e.what();
            return false;
        }
        doc = parsed;
        path = file_path;
        dirty = false;
        ++doc_version;
        ClearTextBufs();
        action_msg = "Loaded " + file_path + ".";
        return true;
    }

    bool SaveToFile(const std::string &file_path) {
        std::ofstream f(file_path);
        if (!f.is_open()) {
            action_msg = "Cannot write file: " + file_path;
            return false;
        }
        f << doc.dump(2) << '\n';
        if (!f.good()) {
            action_msg = "Failed while writing: " + file_path;
            return false;
        }
        path = file_path;
        dirty = false;
        action_msg = "Saved " + file_path + ".";
        return true;
    }

    // Live validation: the loader (Config::from_json + validate())
    // stays authoritative for the pass/fail verdict that gates Export
    // and Launch, while ConfigDiagnostics::Diagnose() supplies the
    // detailed problem list shown in the Problems tab and under each
    // instruction. Diagnose() never throws on malformed documents, so
    // it also runs when the loader cannot parse the document at all.
    void RefreshValidation() {
        const std::vector<ConfigDiagnostics::Issue> issues =
            ConfigDiagnostics::Diagnose(doc);
        problem_count = static_cast<int>(issues.size());
        problems = issues;
        try {
            Config cfg = Config::from_json(doc);
            valid = cfg.validate();
        } catch (const std::exception &e) {
            valid = false;
            validation_msg =
                std::string("Cannot parse/validate: ") + e.what();
            if (!issues.empty())
                validation_msg += " (" + std::to_string(issues.size()) +
                                  (issues.size() == 1 ? " problem"
                                                     : " problems") +
                                  "; see the Problems tab.)";
            last_validated_version = doc_version;
            return;
        }
        if (issues.empty()) {
            validation_msg =
                valid ? "Valid configuration."
                      : "Invalid CPU configuration (see the Problems "
                        "tab).";
        } else {
            validation_msg = std::to_string(issues.size()) +
                             (issues.size() == 1 ? " problem: "
                                                 : " problems: ") +
                             issues.front().problem;
        }
        last_validated_version = doc_version;
    }
};

namespace ConfiguratorDetail {

inline std::string &TextBuffer(ConfiguratorState &st, const std::string &key,
                               const std::string &current) {
    auto &buf = st.text_bufs[key];
    if (buf.capacity() < 256)
        buf.reserve(256);
    if (buf != current && st.active_text_key != key &&
        st.text_no_resync.count(key) == 0)
        buf = current;
    return buf;
}

inline bool InputTextBuf(ConfiguratorState &st, const char *label,
                         const std::string &key, std::string &value,
                         ImGuiInputTextFlags flags = 0) {
    std::string &buf = TextBuffer(st, key, value);
    const bool changed = ImGui::InputText(
        label, buf.data(), buf.capacity() + 1,
        flags | ImGuiInputTextFlags_CallbackResize, TextResizeCallback, &buf);
    if (ImGui::IsItemActive())
        st.active_text_key = key;
    if (changed)
        value = buf;
    return changed;
}

inline bool InputTextMultilineBuf(ConfiguratorState &st, const char *label,
                                  const std::string &key, std::string &value,
                                  const ImVec2 &size) {
    std::string &buf = TextBuffer(st, key, value);
    if (buf.capacity() < 8192)
        buf.reserve(8192);
    const bool changed = ImGui::InputTextMultiline(
        label, buf.data(), buf.capacity() + 1, size,
        ImGuiInputTextFlags_CallbackResize, TextResizeCallback, &buf);
    if (ImGui::IsItemActive())
        st.active_text_key = key;
    if (changed)
        value = buf;
    return changed;
}

// Edit a JSON string field; erases the key when the text is cleared and
// `erase_when_empty` is set.
inline void EditJsonString(ConfiguratorState &st, const char *label,
                           nlohmann::json &obj, const char *key,
                           const std::string &id,
                           bool erase_when_empty = false) {
    std::string v = obj.value(key, std::string(""));
    if (InputTextBuf(st, label, id, v)) {
        if (v.empty() && erase_when_empty)
            obj.erase(key);
        else
            obj[key] = v;
        st.MarkDirty();
    }
}

inline void EditJsonWordText(ConfiguratorState &st, const char *label,
                             nlohmann::json &obj, const char *key,
                             const std::string &id) {
    std::string v = obj.contains(key) ? JsonScalarToString(obj[key]) : "0";
    if (InputTextBuf(st, label, id, v)) {
        CommitWordText(obj[key], v);
        st.MarkDirty();
    }
}

inline void EditJsonInt(ConfiguratorState &st, const char *label,
                        nlohmann::json &obj, const char *key, int def,
                        int lo, int hi) {
    int v = obj.value(key, def);
    if (ImGui::InputInt(label, &v)) {
        v = std::clamp(v, lo, hi);
        obj[key] = v;
        st.MarkDirty();
    }
}

inline void EditJsonCombo(ConfiguratorState &st, const char *label,
                          nlohmann::json &obj, const char *key,
                          const char *const *items, int count,
                          const std::string &def) {
    const std::string cur = obj.value(key, def);
    int idx = 0;
    for (int i = 0; i < count; ++i)
        if (cur == items[i])
            idx = i;
    if (ImGui::Combo(label, &idx, items, count)) {
        obj[key] = items[idx];
        st.MarkDirty();
    }
}

inline void EditJsonBool(ConfiguratorState &st, const char *label,
                         nlohmann::json &obj, const char *key, bool def) {
    bool b = obj.value(key, def);
    if (ImGui::Checkbox(label, &b)) {
        obj[key] = b;
        st.MarkDirty();
    }
}

// Edit a JSON object of string->string as "key = value" lines. On a parse
// error the document is left alone and the message is shown; the typed
// text is kept (no_resync) so the user can fix it.
inline void EditJsonKeyValueLines(ConfiguratorState &st, const char *label,
                                  nlohmann::json &obj, const char *key,
                                  const std::string &id, bool numeric_values) {
    const nlohmann::json current =
        obj.contains(key) ? obj[key] : nlohmann::json::object();
    std::string text = KeyValueMapToText(current);
    if (InputTextMultilineBuf(st, label, id, text, ImVec2(-1, 180))) {
        std::vector<std::pair<std::string, std::string>> pairs;
        std::string error;
        if (!ParseKeyValueLines(text, pairs, error)) {
            st.action_msg = error;
            st.text_no_resync.insert(id);
            return;
        }
        st.text_no_resync.erase(id);
        nlohmann::json rebuilt = nlohmann::json::object();
        for (const auto &[k, v] : pairs) {
            if (numeric_values) {
                nlohmann::json slot = 0;
                CommitWordText(slot, v);
                rebuilt[k] = slot;
            } else {
                rebuilt[k] = v;
            }
        }
        if (rebuilt.empty())
            obj.erase(key);
        else
            obj[key] = rebuilt;
        st.MarkDirty();
    }
}

// Edit an arbitrary JSON value (AST nodes for on_read/on_write/
// tick_behavior) as pretty-printed raw JSON text.
inline void EditJsonRaw(ConfiguratorState &st, const char *label,
                        nlohmann::json &obj, const char *key,
                        const std::string &id) {
    const nlohmann::json current =
        obj.contains(key) ? obj[key] : nlohmann::json::array();
    std::string text = current.dump(2);
    if (InputTextMultilineBuf(st, label, id, text, ImVec2(-1, 240))) {
        try {
            const nlohmann::json parsed = nlohmann::json::parse(text);
            st.text_no_resync.erase(id);
            obj[key] = parsed;
            st.MarkDirty();
        } catch (const std::exception &e) {
            st.action_msg = std::string("Invalid JSON: ") + e.what();
            st.text_no_resync.insert(id);
        }
    }
}

} // namespace ConfiguratorDetail

namespace ConfiguratorDetail {

inline void DrawSystemTab(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    EditJsonString(st, "Name", doc, "name", "sys:name");
    ImGui::Separator();
    ImGui::Text("Bus widths (bits, multiple of 4, 4-128):");
    if (doc.contains("data_bus"))
        EditJsonInt(st, "Data bus width", doc["data_bus"], "width", 8, 4,
                    128);
    if (doc.contains("address_bus"))
        EditJsonInt(st, "Address bus width", doc["address_bus"], "width", 16,
                    4, 128);
    if (doc.contains("instruction_set")) {
        int def_opcode = 8;
        if (doc.contains("data_bus") && doc["data_bus"].contains("width"))
            def_opcode = doc["data_bus"].value("width", 8) >= 8
                             ? 8
                             : doc["data_bus"].value("width", 8);
        EditJsonInt(st, "Opcode width", doc["instruction_set"],
                    "opcode_width", def_opcode, 1, 16);
        ImGui::SameLine();
        ImGui::TextDisabled("(instruction_set.opcode_width)");
    }
    ImGui::TextWrapped("Note: opcode_width defaults to 8 (or the data-bus "
                       "width below 8 bits) when the key is absent.");
}

// Recursive register editor. `path` is a stable id prefix for text buffers.
inline void DrawRegisterEditor(ConfiguratorState &st, nlohmann::json &reg,
                               bool is_top, const std::string &path,
                               int depth) {
    ImGui::PushID(path.c_str());
    EditJsonString(st, "Name", reg, "name", path + "/name");
    EditJsonInt(st, "Width (bits)", reg, "width", 8, 1, 128);
    EditJsonWordText(st, "Initial", reg, "initial", path + "/initial");
    {
        static const char *roles[] = {"",       "program_counter", "pc",
                                      "stack_pointer", "sp", "status_flags",
                                      "flags",  "ir", "accumulator"};
        const std::string cur = reg.value("role", "");
        int idx = 0;
        for (int i = 0; i < 9; ++i)
            if (cur == roles[i])
                idx = i;
        if (ImGui::Combo("Role", &idx, roles, 9)) {
            if (idx == 0)
                reg.erase("role");
            else
                reg["role"] = roles[idx];
            st.MarkDirty();
        }
    }
    EditJsonBool(st, "Coprocessor register", reg, "coproc", false);
    if (reg.value("coproc", false)) {
        EditJsonInt(st, "Coproc ID", reg, "coproc_id", -1, -1, 65535);
        EditJsonInt(st, "Coproc register ID", reg, "coproc_reg_id", -1, -1,
                    65535);
    }
    if (!is_top) {
        static const char *slice_modes[] = {"offset", "mask"};
        const int mode = reg.contains("mask") ? 1 : 0;
        int idx = mode;
        if (ImGui::Combo("Slice by", &idx, slice_modes, 2)) {
            if (idx == 0) {
                reg.erase("mask");
                reg["offset"] = 0;
            } else {
                reg.erase("offset");
                reg["mask"] = "0b1";
            }
            st.MarkDirty();
        }
        if (idx == 0)
            EditJsonInt(st, "Offset (bits)", reg, "offset", 0, 0, 127);
        else
            EditJsonString(st, "Mask (e.g. 0b0101)", reg, "mask",
                           path + "/mask");
    }
    if (reg.contains("sub_registers")) {
        ImGui::Separator();
        ImGui::Text("Sub-registers:");
        int erase_child = -1;
        for (size_t i = 0; i < reg["sub_registers"].size(); ++i) {
            nlohmann::json &child = reg["sub_registers"][i];
            const std::string child_path =
                path + "/sub/" + std::to_string(i);
            const std::string label =
                child.value("name", "sub") + "###sub" + std::to_string(i);
            if (ImGui::TreeNode(label.c_str())) {
                DrawRegisterEditor(st, child, false, child_path, depth + 1);
                if (ImGui::SmallButton("Remove this sub-register"))
                    erase_child = static_cast<int>(i);
                ImGui::TreePop();
            }
        }
        if (erase_child >= 0) {
            reg["sub_registers"].erase(
                reg["sub_registers"].begin() + erase_child);
            st.ClearTextBufs();
            st.MarkDirty();
            ImGui::PopID();
            return;
        }
    }
    // Bit bar: where the sub-registers sit inside this register.
    // Bit 0 is on the right, one block per used parent bit.
    {
        const std::vector<RegisterLayout::SubSpan> spans =
            RegisterLayout::SubRegisterSpans(reg);
        if (!spans.empty()) {
            const int pw = reg.value("width", 0);
            ImGui::Separator();
            ImGui::Text("Bit layout (%d bits, bit 0 on the right):", pw);
            static const ImU32 kPalette[] = {
                IM_COL32(66, 135, 245, 255), IM_COL32(76, 175, 80, 255),
                IM_COL32(255, 152, 0, 255), IM_COL32(156, 39, 176, 255),
                IM_COL32(0, 188, 212, 255), IM_COL32(233, 30, 99, 255),
                IM_COL32(139, 195, 74, 255), IM_COL32(255, 235, 59, 255)};
            const float bar_h = ImGui::GetTextLineHeight() * 1.6f;
            const float bar_w = ImGui::GetContentRegionAvail().x;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(
                pos, ImVec2(pos.x + bar_w, pos.y + bar_h),
                IM_COL32(90, 90, 90, 255)); // uncovered bits: grey
            // First owner per bit, and whether more than one
            // sub-register claims it.
            std::vector<int> owner(static_cast<size_t>(pw), -1);
            std::vector<bool> clash(static_cast<size_t>(pw), false);
            for (size_t s = 0; s < spans.size(); ++s)
                for (int b : spans[s].bits) {
                    if (b < 0 || b >= pw)
                        continue;
                    if (owner[static_cast<size_t>(b)] < 0)
                        owner[static_cast<size_t>(b)] = static_cast<int>(s);
                    else
                        clash[static_cast<size_t>(b)] = true;
                }
            const float bit_w = pw > 0 ? bar_w / static_cast<float>(pw) : 0;
            for (int b = 0; b < pw; ++b) {
                const int o = owner[static_cast<size_t>(b)];
                if (o < 0)
                    continue;
                // Bit 0 on the right.
                const float x0 =
                    pos.x + static_cast<float>(pw - 1 - b) * bit_w;
                const ImU32 col =
                    clash[static_cast<size_t>(b)]
                        ? IM_COL32(220, 60, 60, 255) // overlap: red
                        : kPalette[static_cast<size_t>(o) %
                                   (sizeof(kPalette) / sizeof(*kPalette))];
                dl->AddRectFilled(ImVec2(x0 + 1, pos.y + 1),
                                  ImVec2(x0 + bit_w - 1, pos.y + bar_h - 1),
                                  col);
            }
            dl->AddRect(pos, ImVec2(pos.x + bar_w, pos.y + bar_h),
                        IM_COL32(200, 200, 200, 255));
            // End ticks: highest bit on the left, bit 0 on the right.
            const float tick_h = ImGui::GetTextLineHeight();
            dl->AddText(ImVec2(pos.x, pos.y + bar_h + 2),
                        IM_COL32(160, 160, 160, 255),
                        std::to_string(pw - 1).c_str());
            dl->AddText(ImVec2(pos.x + bar_w -
                                   ImGui::CalcTextSize("0").x,
                               pos.y + bar_h + 2),
                        IM_COL32(160, 160, 160, 255), "0");
            ImGui::Dummy(ImVec2(bar_w, bar_h + tick_h + 2));
            for (size_t s = 0; s < spans.size(); ++s) {
                const RegisterLayout::SubSpan &sp = spans[s];
                std::string range;
                if (sp.bits.empty()) {
                    range = "no bits in range";
                } else if (sp.is_mask) {
                    range = "bits {";
                    for (size_t i = 0; i < sp.bits.size(); ++i) {
                        if (i > 0)
                            range += ", ";
                        range += std::to_string(sp.bits[i]);
                    }
                    range += "} (mask)";
                } else {
                    range = "bits " + std::to_string(sp.last_bit) + ".." +
                            std::to_string(sp.first_bit);
                }
                if (sp.overlaps)
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                       "%s: %s  (overlaps another "
                                       "sub-register)",
                                       sp.name.c_str(), range.c_str());
                else if (sp.out_of_range)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                       "%s: %s  (extends past this "
                                       "register)",
                                       sp.name.c_str(), range.c_str());
                else
                    ImGui::Text("%s: %s", sp.name.c_str(), range.c_str());
            }
            bool any_overlap = false;
            for (const auto &sp : spans)
                any_overlap = any_overlap || sp.overlaps;
            if (any_overlap)
                ImGui::TextColored(
                    ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    "Red bits are covered by more than one sub-register.");
            ImGui::TextDisabled("Grey bits are not covered by any "
                                "sub-register.");
        }
    }
    if (ImGui::Button("Add sub-register")) {
        if (!reg.contains("sub_registers"))
            reg["sub_registers"] = nlohmann::json::array();
        // Start at the first free bit instead of always offset 0.
        const int off = RegisterLayout::FirstFreeOffset(reg, 1);
        reg["sub_registers"].push_back(
            {{"name", "SUB"}, {"width", 1}, {"offset", off < 0 ? 0 : off}});
        st.ClearTextBufs();
        st.MarkDirty();
    }
    ImGui::PopID();
}

inline void DrawRegistersTab(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    if (!doc.contains("registers") || !doc["registers"].is_object())
        doc["registers"] = {{"general_purpose", nlohmann::json::array()},
                            {"special", nlohmann::json::array()}};
    static const char *groups[] = {"general_purpose", "special"};
    static const char *group_labels[] = {"General purpose", "Special"};

    ImGui::BeginChild("reg_list", ImVec2(230, 0), true);
    for (int g = 0; g < 2; ++g) {
        ImGui::Text("%s:", group_labels[g]);
        nlohmann::json &arr = doc["registers"][groups[g]];
        for (size_t i = 0; i < arr.size(); ++i) {
            const std::string label =
                arr[i].value("name", "?") + "###reg" + std::to_string(g) +
                "_" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), st.sel_group == g &&
                                                     st.sel_register ==
                                                         static_cast<int>(i))) {
                st.sel_group = g;
                st.sel_register = static_cast<int>(i);
            }
        }
        const std::string add_label = std::string("+ Add ") +
                                      group_labels[g] + " register";
        if (ImGui::SmallButton(add_label.c_str())) {
            nlohmann::json fresh = {{"name", "R_NEW"},
                                    {"width", 8},
                                    {"initial", 0}};
            if (g == 1)
                fresh["role"] = "";
            arr.push_back(fresh);
            st.sel_group = g;
            st.sel_register = static_cast<int>(arr.size()) - 1;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("reg_editor", ImVec2(0, 0), true);
    nlohmann::json &arr = doc["registers"][groups[st.sel_group]];
    if (!arr.empty()) {
        st.sel_register =
            std::clamp(st.sel_register, 0, static_cast<int>(arr.size()) - 1);
        nlohmann::json &reg = arr[st.sel_register];
        ImGui::Text("%s register: %s", group_labels[st.sel_group],
                    reg.value("name", "?").c_str());
        DrawRegisterEditor(st, reg, true,
                           std::string(groups[st.sel_group]) + "/" +
                               std::to_string(st.sel_register),
                           0);
        ImGui::Separator();
        if (ImGui::Button("Remove this register")) {
            arr.erase(arr.begin() + st.sel_register);
            st.sel_register = 0;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    } else {
        ImGui::TextDisabled("No registers in this group yet.");
    }
    ImGui::EndChild();
}

inline void DrawMemoryTab(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    if (!doc.contains("memory") || !doc["memory"].is_object())
        doc["memory"] = nlohmann::json::object();
    nlohmann::json &mem = doc["memory"];
    EditJsonWordText(st, "Memory size (bytes)", mem, "size", "mem:size");
    {
        static const char *archs[] = {"von_neumann", "harvard"};
        EditJsonCombo(st, "Architecture", mem, "architecture", archs, 2,
                      "von_neumann");
    }
    {
        static const char *ends[] = {"little", "big"};
        EditJsonCombo(st, "Endianness", mem, "endianness", ends, 2, "little");
    }
    ImGui::Separator();
    ImGui::Text("Memory segments:");
    if (!mem.contains("segments") || !mem["segments"].is_array())
        mem["segments"] = nlohmann::json::array();
    nlohmann::json &segs = mem["segments"];
    ImGui::BeginChild("seg_list", ImVec2(200, 0), true);
    for (size_t i = 0; i < segs.size(); ++i) {
        const std::string label = segs[i].value("name", "?") + "###seg" +
                                  std::to_string(i);
        if (ImGui::Selectable(label.c_str(),
                              st.sel_segment == static_cast<int>(i)))
            st.sel_segment = static_cast<int>(i);
    }
    if (ImGui::SmallButton("+ Add segment")) {
        segs.push_back({{"name", "SEG"},
                        {"start", "0x0000"},
                        {"end", "0xFFFF"},
                        {"R", true},
                        {"W", true},
                        {"X", false}});
        st.sel_segment = static_cast<int>(segs.size()) - 1;
        st.ClearTextBufs();
        st.MarkDirty();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("seg_editor", ImVec2(0, 0), true);
    if (!segs.empty()) {
        st.sel_segment =
            std::clamp(st.sel_segment, 0, static_cast<int>(segs.size()) - 1);
        nlohmann::json &seg = segs[st.sel_segment];
        const std::string base = "seg/" + std::to_string(st.sel_segment);
        EditJsonString(st, "Name", seg, "name", base + "/name");
        EditJsonWordText(st, "Start", seg, "start", base + "/start");
        EditJsonWordText(st, "End", seg, "end", base + "/end");
        EditJsonBool(st, "Read (R)", seg, "R", true);
        EditJsonBool(st, "Write (W)", seg, "W", true);
        EditJsonBool(st, "Execute (X)", seg, "X", true);
        ImGui::Separator();
        if (ImGui::Button("Remove this segment")) {
            segs.erase(segs.begin() + st.sel_segment);
            st.sel_segment = 0;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    } else {
        ImGui::TextDisabled("No segments. (An empty list means one flat "
                            "RAM segment covering all memory.)");
    }
    ImGui::EndChild();
}

} // namespace ConfiguratorDetail

namespace ConfiguratorDetail {

inline void DrawAluTab(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    if (!doc.contains("alu") || !doc["alu"].is_object())
        doc["alu"] = {{"flags", nlohmann::json::array()},
                      {"operations", nlohmann::json::array()}};
    nlohmann::json &alu = doc["alu"];
    if (!alu.contains("flags"))
        alu["flags"] = nlohmann::json::array();
    if (!alu.contains("operations"))
        alu["operations"] = nlohmann::json::array();

    ImGui::Text("Flags:");
    ImGui::BeginChild("flag_list", ImVec2(180, 180), true);
    for (size_t i = 0; i < alu["flags"].size(); ++i) {
        const std::string label = alu["flags"][i].value("name", "?") +
                                  "###flag" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(),
                              st.sel_flag == static_cast<int>(i)))
            st.sel_flag = static_cast<int>(i);
    }
    if (ImGui::SmallButton("+ Add flag")) {
        alu["flags"].push_back(
            {{"name", "F"}, {"bit", 0}, {"type", "zero"}});
        st.sel_flag = static_cast<int>(alu["flags"].size()) - 1;
        st.ClearTextBufs();
        st.MarkDirty();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("flag_editor", ImVec2(0, 180), true);
    if (!alu["flags"].empty()) {
        st.sel_flag =
            std::clamp(st.sel_flag, 0, static_cast<int>(alu["flags"].size()) - 1);
        nlohmann::json &flag = alu["flags"][st.sel_flag];
        const std::string base = "flag/" + std::to_string(st.sel_flag);
        EditJsonString(st, "Name", flag, "name", base + "/name");
        EditJsonInt(st, "Bit", flag, "bit", 0, 0, 127);
        EditJsonString(st, "Type", flag, "type", base + "/type");
        EditJsonString(st, "Expression (optional)", flag, "expression",
                       base + "/expression", true);
        if (ImGui::Button("Remove this flag")) {
            alu["flags"].erase(alu["flags"].begin() + st.sel_flag);
            st.sel_flag = 0;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    } else {
        ImGui::TextDisabled("No flags defined.");
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("Operations:");
    ImGui::BeginChild("op_list", ImVec2(180, 0), true);
    for (size_t i = 0; i < alu["operations"].size(); ++i) {
        const std::string label = alu["operations"][i].value("name", "?") +
                                  "###op" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(),
                              st.sel_alu_op == static_cast<int>(i)))
            st.sel_alu_op = static_cast<int>(i);
    }
    if (ImGui::SmallButton("+ Add operation")) {
        const int code =
            FirstFreeCode(alu["operations"], "code", 255);
        if (code < 0) {
            st.action_msg = "No free ALU code left (0-255 are all used).";
        } else {
            alu["operations"].push_back({{"name", "OP"},
                                         {"code", code},
                                         {"expression", "a + b"},
                                         {"latency", 1}});
            st.sel_alu_op = static_cast<int>(alu["operations"].size()) - 1;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("op_editor", ImVec2(0, 0), true);
    if (!alu["operations"].empty()) {
        st.sel_alu_op = std::clamp(st.sel_alu_op, 0,
                                   static_cast<int>(alu["operations"].size()) - 1);
        nlohmann::json &op = alu["operations"][st.sel_alu_op];
        const std::string base = "aluop/" + std::to_string(st.sel_alu_op);
        EditJsonString(st, "Name", op, "name", base + "/name");
        EditJsonInt(st, "Code", op, "code", 0, 0, 255);
        EditJsonInt(st, "Latency (cycles)", op, "latency", 1, 1, 1000000);
        const int mode = op.contains("program") ? 1 : 0;
        if (ImGui::RadioButton("Expression", mode == 0)) {
            if (mode == 1) {
                std::string first = "a + b";
                if (op["program"].is_array() && !op["program"].empty())
                    first = op["program"][0].get<std::string>();
                else if (op["program"].is_string())
                    first = op["program"].get<std::string>();
                op.erase("program");
                op["expression"] = first;
                st.ClearTextBufs();
                st.MarkDirty();
            }
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Program (mini-language)", mode == 1)) {
            if (mode == 0) {
                const std::string expr = op.value("expression", "a + b");
                op.erase("expression");
                op["program"] = nlohmann::json::array({expr});
                st.ClearTextBufs();
                st.MarkDirty();
            }
        }
        if (op.contains("program")) {
            std::string prog_text;
            if (op["program"].is_string())
                prog_text = op["program"].get<std::string>();
            else if (op["program"].is_array()) {
                std::vector<std::string> lines;
                for (const auto &l : op["program"])
                    lines.push_back(l.get<std::string>());
                prog_text = JoinLines(lines);
            }
            ImGui::Text("Program (one statement per line):");
            if (InputTextMultilineBuf(st, "##program", base + "/program",
                                      prog_text, ImVec2(-1, 240))) {
                op["program"] = SplitLines(prog_text);
                st.MarkDirty();
            }
        } else {
            EditJsonString(st, "Expression", op, "expression",
                           base + "/expression");
        }
        ImGui::Text("Flag rules (one per line, e.g. Z = result_zero):");
        EditJsonKeyValueLines(st, "##flagrules", op, "flag_rules",
                              base + "/flag_rules", false);
        ImGui::Separator();
        ImGui::TextWrapped(
            "Inputs a, b, c are automatic: they are bound at run time by "
            "the microcode 'alu' action of the instruction that uses this "
            "operation (its a/b/c arguments; the result width comes from "
            "its 'out'). You do not declare them here. The latency *type* "
            "(mode) is not set on the operation either: it lives on each "
            "instruction, in the Instructions tab.");
        ImGui::Separator();
        if (ImGui::Button("Remove this operation")) {
            alu["operations"].erase(alu["operations"].begin() + st.sel_alu_op);
            st.sel_alu_op = 0;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    } else {
        ImGui::TextDisabled("No ALU operations defined.");
    }
    ImGui::EndChild();
}

inline std::string EncodingToText(const nlohmann::json &enc) {
    std::string out;
    if (!enc.is_array())
        return out;
    for (size_t i = 0; i < enc.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += JsonScalarToString(enc[i]);
    }
    return out;
}

inline bool ParseEncodingText(const std::string &text, nlohmann::json &out,
                              std::string &error) {
    out = nlohmann::json::array();
    for (const std::string &raw : SplitLines(text)) {
        // Commas and newlines both separate tokens.
        std::string token;
        for (size_t i = 0; i <= raw.size(); ++i) {
            if (i == raw.size() || raw[i] == ',') {
                const std::string t = Trim(token);
                token.clear();
                if (t.empty())
                    continue;
                try {
                    size_t used = 0;
                    const long long v = std::stoll(t, &used);
                    if (used == t.size()) {
                        out.push_back(v);
                        continue;
                    }
                } catch (...) {
                }
                out.push_back(t);
            } else {
                token += raw[i];
            }
        }
    }
    if (out.empty()) {
        error = "Encoding must contain at least the opcode.";
        return false;
    }
    return true;
}

inline std::string JoinComma(const std::vector<std::string> &items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += items[i];
    }
    return out;
}

// Instruction preview: assembly form, encoding, per-micro-op
// inputs -> outputs with cycle counts, effective latency, and the two
// latency warnings. Pure derivation lives in instruction_preview.hpp;
// this only renders it. Errors live in the Problems list below, not
// here, so nothing is reported twice.
inline void DrawInstructionPreview(const nlohmann::json &doc,
                                   const nlohmann::json &inst) {
    const InstructionPreview::Preview pv =
        InstructionPreview::Build(doc, inst);
    ImGui::Text("Preview:");
    ImGui::Text("  %s", pv.assembly.c_str());
    ImGui::TextDisabled("  Encoding: %s",
                        JoinComma(pv.encoding_tokens).c_str());
    if (ImGui::BeginTable("inst_preview", 5,
                          ImGuiTableFlags_Borders |
                              ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Micro-op");
        ImGui::TableSetupColumn("Inputs");
        ImGui::TableSetupColumn("Outputs");
        ImGui::TableSetupColumn("Cycles");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < pv.uops.size(); ++i) {
            const auto &u = pv.uops[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", u.summary.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", JoinComma(u.inputs).c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", JoinComma(u.outputs).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", u.latency);
        }
        ImGui::EndTable();
    }
    ImGui::Text("Effective latency: %d cycles (%s)",
                pv.effective_cycles, pv.effective_explanation.c_str());
    ImGui::TextWrapped("Mode as written: '%s'; mode applied by the loader: "
                       "'%s'.",
                       pv.mode_raw.c_str(), pv.mode_effective.c_str());
    for (const auto &w : pv.warnings)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Warning: %s",
                           w.c_str());
}

// Problems of one instruction: each issue as a red problem line followed
// by its concrete fix. Rendered under the instruction's Preview panel.
inline void DrawInstructionProblems(const nlohmann::json &doc,
                                    const nlohmann::json &inst) {
    const std::vector<ConfigDiagnostics::Issue> issues =
        ConfigDiagnostics::DiagnoseInstruction(doc, inst);
    ImGui::Text("Problems:");
    if (issues.empty()) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                           "No problems in this instruction.");
        return;
    }
    for (const auto &issue : issues) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                           issue.problem.c_str());
        ImGui::TextWrapped("Fix: %s", issue.fix.c_str());
    }
}

// All problems in the document, grouped by area. Rendered as the
// "Problems" tab; the top status line only shows the count and the
// first problem. The list is cached in the state by RefreshValidation,
// so opening the tab does not re-run the diagnosis.
inline void DrawProblemsTab(ConfiguratorState &st) {
    if (st.problems.empty()) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "No problems.");
        return;
    }
    static const char *areas[] = {"System", "Registers", "ALU",
                                  "Instructions", "Memory", "Peripherals"};
    for (const char *area : areas) {
        bool first_in_area = true;
        for (const auto &issue : st.problems) {
            if (ConfigDiagnostics::AreaOf(issue) != area)
                continue;
            if (first_in_area) {
                ImGui::Separator();
                ImGui::Text("%s:", area);
                first_in_area = false;
            }
            ImGui::TextWrapped("%s", issue.where.c_str());
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                               issue.problem.c_str());
            ImGui::TextWrapped("Fix: %s", issue.fix.c_str());
        }
    }
    // Any issue whose area is not one of the six above (none are
    // expected, but a future rule should not silently disappear).
    bool other_heading_shown = false;
    for (const auto &issue : st.problems) {
        const std::string area = ConfigDiagnostics::AreaOf(issue);
        bool known = false;
        for (const char *a : areas)
            if (area == a)
                known = true;
        if (known)
            continue;
        if (!other_heading_shown) {
            ImGui::Separator();
            ImGui::Text("Other:");
            other_heading_shown = true;
        }
        ImGui::TextWrapped("%s", issue.where.c_str());
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                           issue.problem.c_str());
        ImGui::TextWrapped("Fix: %s", issue.fix.c_str());
    }
}

// One micro-op argument as a guided field: a combo over the valid
// values for that slot (see microop_editor.hpp), plus a free-text
// field whenever the current value is not among them -- a literal
// (#...), or a value typed before the action changed -- so nothing
// expressible in JSON becomes impossible.
inline void EditMicroOpArg(ConfiguratorState &st, const nlohmann::json &doc,
                           const nlohmann::json &inst, nlohmann::json &uop,
                           const MicroOpEditor::ArgSpec &spec,
                           const std::string &ubase) {
    const std::string key = spec.key;
    const std::string cur = uop.value(key, std::string(""));
    if (spec.kind == MicroOpEditor::ArgKind::kBool) {
        // Boolean flags are stored as the strings "true"/"false".
        bool b = (cur == "true");
        if (ImGui::Checkbox(spec.label.c_str(), &b)) {
            uop[key] = b ? "true" : "false";
            st.MarkDirty();
        }
        return;
    }
    std::vector<std::string> options;
    bool literal_entry = false;
    if (spec.kind == MicroOpEditor::ArgKind::kOperand) {
        options = MicroOpEditor::OperandOptions(doc, inst, spec.writable);
        literal_entry = true;
    } else if (spec.kind == MicroOpEditor::ArgKind::kAluOp) {
        options = MicroOpEditor::AluOpNames(doc);
    } else { // kCondition
        options = MicroOpEditor::ConditionOptions(doc);
    }
    // Items: an optional argument offers "(not set)" first; then the
    // valid options; then "# (literal...)" for operands; then
    // "Custom...". An unset optional argument (cur == "") used to fall
    // through to "Custom..." and show an empty, unlabeled text box
    // underneath -- those were the mystery empty fields. Now it shows
    // "(not set)" and no box at all.
    const bool can_unset = !spec.required;
    const int base = can_unset ? 1 : 0;
    std::vector<std::string> items;
    if (can_unset)
        items.push_back("(not set)");
    for (const auto &o : options)
        items.push_back(o);
    if (literal_entry)
        items.push_back("# (literal...)");
    items.push_back("Custom...");
    int idx = static_cast<int>(items.size()) - 1; // "Custom..." default
    if (cur.empty() && can_unset) {
        idx = 0; // "(not set)"
    } else {
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (items[i] == cur) {
                idx = i;
                break;
            }
        if (literal_entry && !cur.empty() && cur[0] == '#')
            idx = base + static_cast<int>(options.size());
    }
    std::vector<const char *> citems;
    for (const auto &s : items)
        citems.push_back(s.c_str());
    // Label above the combo, not beside it: the editor column is
    // narrow and the beside-label was clipped at the window edge.
    ImGui::Text("%s", spec.label.c_str());
    if (ImGui::Combo(("##arg_" + key).c_str(), &idx, citems.data(),
                     static_cast<int>(citems.size()))) {
        if (can_unset && idx == 0) {
            uop.erase(key); // genuinely unset, not an empty string
        } else if (idx >= base &&
                   idx < base + static_cast<int>(options.size())) {
            uop[key] = options[idx - base];
        } else if (literal_entry &&
                   idx == base + static_cast<int>(options.size())) {
            uop[key] = "#0"; // editable as free text below
        } else {
            uop[key] = ""; // blank custom value, typed below
        }
        st.MarkDirty();
    }
    // Free-text box only when the value is a literal or a custom
    // value, with a visible label saying what it is.
    if (MicroOpEditor::ShowsTextBox(doc, inst, spec, cur)) {
        const bool is_literal = literal_entry && cur[0] == '#';
        ImGui::Text("%s",
                    is_literal ? "Literal value" : "Custom value");
        std::string v = cur;
        if (InputTextBuf(st, ("##custom_" + key).c_str(),
                         ubase + "/arg/" + key, v)) {
            uop[key] = v;
            st.MarkDirty();
        }
    }
}

inline void DrawInstructionsTab(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    if (!doc.contains("instruction_set") ||
        !doc["instruction_set"].is_object())
        doc["instruction_set"] = nlohmann::json::object();
    nlohmann::json &iset = doc["instruction_set"];
    if (!iset.contains("instructions"))
        iset["instructions"] = nlohmann::json::array();
    nlohmann::json &insts = iset["instructions"];

    ImGui::BeginChild("inst_list", ImVec2(200, 0), true);
    for (size_t i = 0; i < insts.size(); ++i) {
        const std::string label = insts[i].value("name", "?") + "###inst" +
                                  std::to_string(i);
        if (ImGui::Selectable(label.c_str(),
                              st.sel_instruction == static_cast<int>(i)))
            st.sel_instruction = static_cast<int>(i);
    }
    if (ImGui::SmallButton("+ Add instruction")) {
        const int opcode =
            FirstFreeCode(insts, "opcode", 65535);
        if (opcode < 0) {
            st.action_msg =
                "No free instruction opcode left (0-65535 are all used).";
        } else {
            nlohmann::json fresh_inst;
            fresh_inst["name"] = "NEW";
            fresh_inst["opcode"] = opcode;
            // Keep the encoding's leading literal in sync with the
            // opcode, as in every shipped config.
            fresh_inst["encoding"] = nlohmann::json::array({opcode});
            nlohmann::json halt_uop;
            halt_uop["action"] = "halt";
            fresh_inst["microcode"] = nlohmann::json::array({halt_uop});
            insts.push_back(fresh_inst);
            st.sel_instruction = static_cast<int>(insts.size()) - 1;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("inst_editor", ImVec2(0, 0), true);
    if (!insts.empty()) {
        st.sel_instruction = std::clamp(
            st.sel_instruction, 0, static_cast<int>(insts.size()) - 1);
        nlohmann::json &inst = insts[st.sel_instruction];
        const std::string base = "inst/" + std::to_string(st.sel_instruction);
        EditJsonString(st, "Name", inst, "name", base + "/name");
        const int old_opcode = inst.value("opcode", 0);
        EditJsonInt(st, "Opcode", inst, "opcode", 0, 0, 65535);
        if (inst.value("opcode", 0) != old_opcode) {
            SyncEncodingWithOpcode(inst, inst.value("opcode", 0));
            st.ClearTextBufs();
        }
        {
            int lat = inst.value("latency", -1);
            if (ImGui::InputInt("Latency (-1 = none)", &lat)) {
                lat = std::clamp(lat, -1, 1000000);
                if (lat < 0)
                    inst.erase("latency");
                else
                    inst["latency"] = lat;
                st.MarkDirty();
            }
        }
        {
            static const char *modes[] = {"dynamic", "strict", "bottleneck",
                                          "additive"};
            EditJsonCombo(st, "Latency mode", inst, "latency_mode", modes, 4,
                          "dynamic");
        }
        {
            // Plain-words explanation of the mode the loader will
            // actually apply, plus an honest warning when it is ignored.
            const int lat_now = inst.value("latency", -1);
            const std::string mode_raw =
                inst.value("latency_mode", "dynamic");
            const std::string mode_eff =
                InstructionPreview::EffectiveLatencyMode(mode_raw, lat_now);
            ImGui::TextWrapped(
                "%s",
                InstructionPreview::ModeExplanation(mode_eff).c_str());
            if (lat_now <= 0) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                    "Warning: no latency is set, so this latency mode is "
                    "ignored and the loader forces dynamic mode.");
            } else if (mode_raw != mode_eff && mode_raw != "fixed" &&
                       mode_raw != "max") {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                    "Warning: with a latency set, the loader maps mode '%s' "
                    "to '%s'.",
                    mode_raw.c_str(), mode_eff.c_str());
            }
        }
        {
            std::string enc_text = EncodingToText(
                inst.contains("encoding") ? inst["encoding"]
                                          : nlohmann::json::array());
            if (InputTextBuf(st, "Encoding (comma separated, e.g. 5, dest, src)",
                             base + "/encoding", enc_text)) {
                nlohmann::json parsed;
                std::string error;
                if (!ParseEncodingText(enc_text, parsed, error)) {
                    st.action_msg = error;
                    st.text_no_resync.insert(base + "/encoding");
                } else {
                    st.text_no_resync.erase(base + "/encoding");
                    inst["encoding"] = parsed;
                    // The other direction of the same invariant: the
                    // first encoding element is the opcode literal, so
                    // changing it here renumbers the opcode as well.
                    if (!parsed.empty() && parsed[0].is_number_integer()) {
                        const int enc_opcode = parsed[0].get<int>();
                        if (enc_opcode >= 0 && enc_opcode <= 65535)
                            inst["opcode"] = enc_opcode;
                    }
                    st.MarkDirty();
                }
            }
        }
        ImGui::Separator();
        ImGui::Text("Microcode:");
        if (!inst.contains("microcode"))
            inst["microcode"] = nlohmann::json::array();
        nlohmann::json &uops = inst["microcode"];
        // Both panes size to their content instead of a fixed 180px,
        // so the outer instruction editor scrolls rather than the
        // inner boxes. The list is one row per micro-op plus the Add
        // button; the editor is sized to the selected micro-op's
        // fields (label + control per argument, plus a text box where
        // one is shown).
        const float row_h = ImGui::GetTextLineHeightWithSpacing();
        const float child_pad = ImGui::GetStyle().WindowPadding.y * 2.0f;
        const float list_h =
            std::clamp((static_cast<float>(uops.size()) + 1.0f) * row_h +
                           child_pad,
                       60.0f, 400.0f);
        float editor_rows = 2.0f; // Action label + combo
        if (!uops.empty()) {
            const nlohmann::json &sel_uop =
                uops[std::clamp(st.sel_microop, 0,
                                static_cast<int>(uops.size()) - 1)];
            const std::vector<MicroOpEditor::ArgSpec> sel_specs =
                MicroOpEditor::ArgSpecs(sel_uop.value("action", "halt"));
            std::unordered_set<std::string> known;
            for (const auto &s : sel_specs) {
                known.insert(s.key);
                editor_rows += 2.0f;
                if (MicroOpEditor::ShowsTextBox(
                        doc, inst, s,
                        sel_uop.value(s.key, std::string(""))))
                    editor_rows += 2.0f; // label + text box
            }
            for (auto it = sel_uop.begin(); it != sel_uop.end(); ++it)
                if (it.key() != "action" && !known.count(it.key()))
                    editor_rows += 1.0f; // "Other argument" line
            editor_rows += 1.0f;         // Remove button
        }
        const float editor_h =
            std::clamp(editor_rows * row_h + child_pad, 100.0f, 520.0f);
        ImGui::BeginChild("uop_list", ImVec2(220, list_h), true);
        for (size_t i = 0; i < uops.size(); ++i) {
            const std::string label =
                uops[i].value("action", "?") + "###uop" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(),
                                  st.sel_microop == static_cast<int>(i)))
                st.sel_microop = static_cast<int>(i);
        }
        if (ImGui::SmallButton("+ Add micro-op")) {
            uops.push_back({{"action", "halt"}});
            st.sel_microop = static_cast<int>(uops.size()) - 1;
            st.ClearTextBufs();
            st.MarkDirty();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("uop_editor", ImVec2(0, editor_h), true);
        if (!uops.empty()) {
            st.sel_microop = std::clamp(st.sel_microop, 0,
                                        static_cast<int>(uops.size()) - 1);
            nlohmann::json &uop = uops[st.sel_microop];
            const std::string ubase =
                base + "/uop/" + std::to_string(st.sel_microop);
            static const char *actions[] = {
                "copy", "alu", "mem_read", "mem_write", "port_read",
                "port_write", "coproc_read", "coproc_write", "branch", "halt"};
            const std::string old_action = uop.value("action", "halt");
            // Label above the combo, like the argument fields below:
            // the beside-label was clipped at the window edge.
            ImGui::Text("Action");
            EditJsonCombo(st, "##action", uop, "action", actions, 10,
                          "halt");
            const std::string action = uop.value("action", "halt");
            if (action != old_action) {
                // The new action needs different arguments: drop the
                // ones that do not belong to it, and prefill any
                // missing required argument with its first valid
                // picker option.
                const std::vector<MicroOpEditor::ArgSpec> specs =
                    MicroOpEditor::ArgSpecs(action);
                std::unordered_set<std::string> keep;
                for (const auto &s : specs)
                    keep.insert(s.key);
                for (auto it = uop.begin(); it != uop.end();) {
                    if (it.key() != "action" && !keep.count(it.key()))
                        it = uop.erase(it);
                    else
                        ++it;
                }
                for (const auto &s : specs)
                    if (s.required && !uop.contains(s.key)) {
                        const std::string def =
                            MicroOpEditor::DefaultValue(doc, inst, s);
                        if (!def.empty())
                            uop[s.key] = def;
                    }
                st.ClearTextBufs();
                st.MarkDirty();
            }
            // Guided arguments: exactly what this action needs, each
            // as its own picker field. No free-text box to mistype
            // an operand reference in.
            const std::vector<MicroOpEditor::ArgSpec> specs =
                MicroOpEditor::ArgSpecs(action);
            for (const auto &spec : specs)
                EditMicroOpArg(st, doc, inst, uop, spec, ubase);
            if (specs.empty() && action != "halt")
                ImGui::TextDisabled(
                    "Unknown action: pick a known action above, or edit "
                    "this micro-op in the raw JSON tab.");
            // Arguments that are not part of this action (e.g. left
            // over from the raw JSON tab) stay in the document but are
            // not editable here; show them so nothing is hidden.
            {
                std::unordered_set<std::string> known;
                for (const auto &s : specs)
                    known.insert(s.key);
                for (auto it = uop.begin(); it != uop.end(); ++it)
                    if (it.key() != "action" && !known.count(it.key()))
                        ImGui::TextDisabled(
                            "Other argument (not used by this action): "
                            "%s = %s",
                            it.key().c_str(),
                            ConfigDiagnostics::ScalarText(it.value())
                                .c_str());
            }
            if (ImGui::Button("Remove this micro-op")) {
                uops.erase(uops.begin() + st.sel_microop);
                st.sel_microop = 0;
                st.ClearTextBufs();
                st.MarkDirty();
            }
        } else {
            ImGui::TextDisabled("No micro-ops. An instruction needs at "
                                "least one.");
        }
        ImGui::EndChild();
        ImGui::Separator();
        DrawInstructionPreview(doc, inst);
        ImGui::Separator();
        DrawInstructionProblems(doc, inst);
        ImGui::Separator();
        if (ImGui::Button("Remove this instruction")) {
            insts.erase(insts.begin() + st.sel_instruction);
            st.sel_instruction = 0;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    } else {
        ImGui::TextDisabled("No instructions defined.");
    }
    ImGui::EndChild();
}

} // namespace ConfiguratorDetail

namespace ConfiguratorDetail {

// Graphical block editor for one peripheral hook (on_read, on_write or
// tick_behavior): a JSON array of AST nodes. Each hook has its own
// Blocks/Text toggle (default Blocks). Text view is the existing
// EditJsonRaw pretty-printed box; if the typed text does not parse,
// the text is kept, the error is shown, and Blocks stays disabled
// until the JSON parses again. A node with an unknown type is
// preserved as-is and shown greyed-out with a hint to use Text mode.

inline void DrawHookNodeList(ConfiguratorState &st,
                             const nlohmann::json &periph,
                             nlohmann::json &nodes, const std::string &id);
inline void DrawHookNode(ConfiguratorState &st,
                         const nlohmann::json &periph, nlohmann::json &node,
                         const std::string &id, int &erase_out,
                         int &move_out);

// One hook: Blocks/Text toggle, a hint about the `value` context
// variable, then either the node blocks or the raw JSON box.
inline void DrawHookEditor(ConfiguratorState &st,
                           const nlohmann::json &periph, nlohmann::json &obj,
                           const char *key, const std::string &id,
                           const char *value_hint) {
    ImGui::Text("%s (optional):", key);
    const bool bad_json = st.text_no_resync.count(id) > 0;
    const bool not_array = obj.contains(key) && !obj[key].is_array();
    const bool forced_text = bad_json || not_array;
    const bool text_view =
        forced_text || st.hook_text_view.count(id) > 0;
    if (forced_text)
        ImGui::BeginDisabled();
    if (ImGui::SmallButton(text_view ? "Blocks" : "Text")) {
        if (st.hook_text_view.count(id))
            st.hook_text_view.erase(id);
        else
            st.hook_text_view.insert(id);
    }
    if (forced_text)
        ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(
        text_view ? "Text view: the hook as pretty-printed JSON."
                  : "Blocks view: the hook as AST node blocks.");
    ImGui::TextDisabled("%s", value_hint);
    if (text_view) {
        if (bad_json)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "Blocks view is disabled until this JSON "
                               "parses again.");
        if (not_array)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                               "A hook must be a JSON array of nodes.");
        EditJsonRaw(st, ("##" + std::string(key)).c_str(), obj, key, id);
        return;
    }
    if (!obj.contains(key)) {
        ImGui::TextDisabled("No nodes yet.");
        if (ImGui::SmallButton("+ Add node")) {
            obj[key] = nlohmann::json::array(
                {HookEditor::DefaultNode(
                    "assign", HookEditor::AvailableTargets(periph))});
            st.ClearTextBufs();
            st.MarkDirty();
        }
        return;
    }
    DrawHookNodeList(st, periph, obj[key], id);
}

// A vertical list of node blocks, each with move-up / move-down /
// remove, plus a "+ Add node" button (new node defaults to assign).
inline void DrawHookNodeList(ConfiguratorState &st,
                             const nlohmann::json &periph,
                             nlohmann::json &nodes, const std::string &id) {
    int erase_idx = -1, move_idx = -1, move_dir = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        nlohmann::json &node = nodes[i];
        ImGui::PushID(static_cast<int>(i));
        int erase = 0, move = 0;
        DrawHookNode(st, periph, node, id + "/" + std::to_string(i),
                     erase, move);
        if (erase)
            erase_idx = static_cast<int>(i);
        if (move != 0) {
            move_idx = static_cast<int>(i);
            move_dir = move;
        }
        ImGui::PopID();
        if (i + 1 < nodes.size())
            ImGui::Separator();
    }
    if (erase_idx >= 0) {
        nodes.erase(nodes.begin() + erase_idx);
        st.ClearTextBufs();
        st.MarkDirty();
    } else if (move_idx >= 0) {
        const int j = move_idx + move_dir;
        if (j >= 0 && j < static_cast<int>(nodes.size())) {
            std::swap(nodes[move_idx], nodes[j]);
            st.ClearTextBufs();
            st.MarkDirty();
        }
    }
    if (ImGui::SmallButton("+ Add node")) {
        nodes.push_back(HookEditor::DefaultNode(
            "assign", HookEditor::AvailableTargets(periph)));
        st.ClearTextBufs();
        st.MarkDirty();
    }
}

// One node block: a type combo (changing it drops fields that do not
// belong to the new type and prefills the required ones), then exactly
// the fields that type needs. Move-up / move-down / remove buttons sit
// on the type row.
inline void DrawHookNode(ConfiguratorState &st,
                         const nlohmann::json &periph, nlohmann::json &node,
                         const std::string &id, int &erase_out,
                         int &move_out) {
    erase_out = 0;
    move_out = 0;
    if (!HookEditor::IsRepresentable(node)) {
        ImGui::TextDisabled("Unknown node type -- edit in Text mode:");
        ImGui::TextWrapped("%s", node.dump(2).c_str());
    } else {
        const std::string cur_type = node.value("type", "assign");
        const std::vector<std::string> types = HookEditor::NodeTypes();
        int idx = 1; // assign
        for (int i = 0; i < static_cast<int>(types.size()); ++i)
            if (types[i] == cur_type)
                idx = i;
        std::vector<const char *> citems;
        for (const auto &t : types)
            citems.push_back(t.c_str());
        ImGui::Text("Node type");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        if (ImGui::Combo("##type", &idx, citems.data(),
                         static_cast<int>(citems.size()))) {
            node = HookEditor::DefaultNode(
                types[idx], HookEditor::AvailableTargets(periph));
            st.ClearTextBufs();
            st.MarkDirty();
            return;
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##up", ImGuiDir_Up))
            move_out = -1;
        ImGui::SameLine();
        if (ImGui::ArrowButton("##down", ImGuiDir_Down))
            move_out = 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            erase_out = 1;
        if (cur_type == "if") {
            ImGui::Text("condition");
            std::string cond = node.value("condition", "1");
            if (InputTextBuf(st, "##condition", id + "/condition", cond)) {
                node["condition"] = cond;
                st.MarkDirty();
            }
            ImGui::Text("then:");
            ImGui::Indent();
            DrawHookNodeList(st, periph, node["then"], id + "/then");
            ImGui::Unindent();
            if (node.contains("else")) {
                ImGui::Text("else:");
                ImGui::Indent();
                DrawHookNodeList(st, periph, node["else"], id + "/else");
                ImGui::Unindent();
                if (ImGui::SmallButton("Remove else branch")) {
                    node.erase("else");
                    st.ClearTextBufs();
                    st.MarkDirty();
                }
            } else if (ImGui::SmallButton("+ Add else branch")) {
                node["else"] = nlohmann::json::array();
                st.ClearTextBufs();
                st.MarkDirty();
            }
        } else if (cur_type == "assign") {
            const std::vector<std::string> options =
                HookEditor::AvailableTargets(periph);
            std::vector<std::string> items = options;
            items.push_back("Custom...");
            const std::string cur = node.value("target", "");
            int tidx = static_cast<int>(items.size()) - 1;
            for (int i = 0; i < static_cast<int>(options.size()); ++i)
                if (options[i] == cur)
                    tidx = i;
            std::vector<const char *> titems;
            for (const auto &s : items)
                titems.push_back(s.c_str());
            ImGui::Text("target");
            if (ImGui::Combo("##target", &tidx, titems.data(),
                             static_cast<int>(titems.size()))) {
                node["target"] =
                    tidx < static_cast<int>(options.size())
                        ? options[tidx]
                        : "";
                st.MarkDirty();
            }
            if (std::find(options.begin(), options.end(), cur) ==
                options.end()) {
                ImGui::Text("Custom value");
                std::string v = cur;
                if (InputTextBuf(st, "##target_custom",
                                 id + "/target_custom", v)) {
                    node["target"] = v;
                    st.MarkDirty();
                }
            }
            ImGui::Text("expr");
            std::string expr = node.value("expr", "0");
            if (InputTextBuf(st, "##expr", id + "/expr", expr)) {
                node["expr"] = expr;
                st.MarkDirty();
            }
        } else if (cur_type == "call") {
            const std::vector<std::string> funcs = HookEditor::CallFuncs();
            const std::string cur = node.value("func", "trigger_interrupt");
            int fidx = static_cast<int>(funcs.size()); // Custom...
            for (int i = 0; i < static_cast<int>(funcs.size()); ++i)
                if (funcs[i] == cur)
                    fidx = i;
            std::vector<std::string> items = funcs;
            items.push_back("Custom...");
            std::vector<const char *> fitems;
            for (const auto &s : items)
                fitems.push_back(s.c_str());
            ImGui::Text("func");
            if (ImGui::Combo("##func", &fidx, fitems.data(),
                             static_cast<int>(fitems.size()))) {
                const std::string next =
                    fidx < static_cast<int>(funcs.size()) ? funcs[fidx]
                                                          : "";
                node["func"] = next;
                // Pad the arguments to the new function's arity;
                // extras stay.
                const int arity = HookEditor::CallFuncArity(next);
                if (arity >= 0) {
                    if (!node.contains("args") ||
                        !node["args"].is_array())
                        node["args"] = nlohmann::json::array();
                    while (static_cast<int>(node["args"].size()) < arity)
                        node["args"].push_back("0");
                }
                st.MarkDirty();
            }
            if (std::find(funcs.begin(), funcs.end(), cur) ==
                funcs.end()) {
                ImGui::Text("Custom value");
                std::string v = cur;
                if (InputTextBuf(st, "##func_custom", id + "/func_custom",
                                 v)) {
                    node["func"] = v;
                    st.MarkDirty();
                }
            }
            const int arity =
                HookEditor::CallFuncArity(node.value("func", ""));
            if (arity >= 0)
                ImGui::TextDisabled("%s expects %d argument(s).",
                                    node.value("func", "").c_str(), arity);
            ImGui::Text("args:");
            const size_t nargs =
                (node.contains("args") && node["args"].is_array())
                    ? node["args"].size()
                    : 0;
            int erase_arg = -1;
            for (size_t j = 0; j < nargs; ++j) {
                ImGui::PushID(static_cast<int>(j));
                std::string a =
                    node["args"][j].is_string()
                        ? node["args"][j].get<std::string>()
                        : "";
                if (InputTextBuf(st, "##arg",
                                 id + "/args/" + std::to_string(j), a)) {
                    node["args"][j] = a;
                    st.MarkDirty();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                    erase_arg = static_cast<int>(j);
                ImGui::PopID();
            }
            if (erase_arg >= 0) {
                node["args"].erase(node["args"].begin() + erase_arg);
                st.ClearTextBufs();
                st.MarkDirty();
            }
            if (ImGui::SmallButton("+ Add argument")) {
                if (!node.contains("args") || !node["args"].is_array())
                    node["args"] = nlohmann::json::array({"0"});
                else
                    node["args"].push_back("0");
                st.ClearTextBufs();
                st.MarkDirty();
            }
        }
        return;
    }
    // Unknown node: still movable and removable.
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        move_out = -1;
    ImGui::SameLine();
    if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        move_out = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("x"))
        erase_out = 1;
}


inline void DrawPeripheralsTab(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    if (!doc.contains("peripherals") || !doc["peripherals"].is_array())
        doc["peripherals"] = nlohmann::json::array();
    nlohmann::json &periphs = doc["peripherals"];

    ImGui::BeginChild("periph_list", ImVec2(200, 0), true);
    for (size_t i = 0; i < periphs.size(); ++i) {
        const std::string label = periphs[i].value("name", "?") + "###per" +
                                  std::to_string(i);
        if (ImGui::Selectable(label.c_str(),
                              st.sel_peripheral == static_cast<int>(i)))
            st.sel_peripheral = static_cast<int>(i);
    }
    if (ImGui::SmallButton("+ Add peripheral")) {
        periphs.push_back({{"name", "Peripheral"},
                           {"type", "text_display"},
                           {"address_start", "0x0000"},
                           {"address_end", "0x0000"}});
        st.sel_peripheral = static_cast<int>(periphs.size()) - 1;
        st.ClearTextBufs();
        st.MarkDirty();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("periph_editor", ImVec2(0, 0), true);
    if (!periphs.empty()) {
        st.sel_peripheral = std::clamp(st.sel_peripheral, 0,
                                       static_cast<int>(periphs.size()) - 1);
        nlohmann::json &p = periphs[st.sel_peripheral];
        const std::string base = "per/" + std::to_string(st.sel_peripheral);
        EditJsonString(st, "Name", p, "name", base + "/name");
        {
            static const char *types[] = {"text_display", "grid_display",
                                          "input", "declarative"};
            EditJsonCombo(st, "Type", p, "type", types, 4, "text_display");
        }
        if (p.contains("address")) {
            EditJsonWordText(st, "Address", p, "address", base + "/address");
        } else {
            EditJsonWordText(st, "Address start", p, "address_start",
                             base + "/address_start");
            EditJsonWordText(st, "Address end", p, "address_end",
                             base + "/address_end");
        }
        ImGui::Text("Parameters (one per line, e.g. width = 8):");
        EditJsonKeyValueLines(st, "##params", p, "parameters",
                              base + "/parameters", false);
        ImGui::Text("Internal state (one per line, e.g. COUNT = 0):");
        EditJsonKeyValueLines(st, "##internal", p, "internal_state",
                              base + "/internal_state", true);

        ImGui::Separator();
        ImGui::Text("Peripheral registers:");
        if (!p.contains("registers"))
            p["registers"] = nlohmann::json::array();
        nlohmann::json &regs = p["registers"];
        ImGui::BeginChild("preg_list", ImVec2(180, 150), true);
        for (size_t i = 0; i < regs.size(); ++i) {
            const std::string label = regs[i].value("name", "?") + "###preg" +
                                      std::to_string(i);
            if (ImGui::Selectable(label.c_str(),
                                  st.sel_periph_reg == static_cast<int>(i)))
                st.sel_periph_reg = static_cast<int>(i);
        }
        if (ImGui::SmallButton("+ Add register")) {
            regs.push_back({{"name", "REG"},
                            {"offset", 0},
                            {"size_bytes", 1},
                            {"access", "rw"},
                            {"initial", 0}});
            st.sel_periph_reg = static_cast<int>(regs.size()) - 1;
            st.ClearTextBufs();
            st.MarkDirty();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("preg_editor", ImVec2(0, 150), true);
        if (!regs.empty()) {
            st.sel_periph_reg = std::clamp(
                st.sel_periph_reg, 0, static_cast<int>(regs.size()) - 1);
            nlohmann::json &r = regs[st.sel_periph_reg];
            const std::string rbase =
                base + "/preg/" + std::to_string(st.sel_periph_reg);
            EditJsonString(st, "Name", r, "name", rbase + "/name");
            EditJsonInt(st, "Offset", r, "offset", 0, 0, 1000000);
            EditJsonInt(st, "Size (bytes)", r, "size_bytes", 1, 1, 16);
            {
                static const char *acc[] = {"r", "w", "rw"};
                EditJsonCombo(st, "Access", r, "access", acc, 3, "rw");
            }
            EditJsonWordText(st, "Initial", r, "initial", rbase + "/initial");
            if (ImGui::Button("Remove this register")) {
                regs.erase(regs.begin() + st.sel_periph_reg);
                st.sel_periph_reg = 0;
                st.ClearTextBufs();
                st.MarkDirty();
            }
        } else {
            ImGui::TextDisabled("No registers.");
        }
        ImGui::EndChild();

        // AST hooks of the selected peripheral register.
        if (!regs.empty()) {
            nlohmann::json &r = regs[st.sel_periph_reg];
            const std::string rbase =
                base + "/preg/" + std::to_string(st.sel_periph_reg);
            DrawHookEditor(st, p, r, "on_read", rbase + "/on_read",
                           "value = current register value");
            DrawHookEditor(st, p, r, "on_write", rbase + "/on_write",
                           "value = written value");
        }

        ImGui::Separator();
        DrawHookEditor(st, p, p, "tick_behavior", base + "/tick",
                       "no context value in tick_behavior");
        ImGui::Separator();
        if (ImGui::Button("Remove this peripheral")) {
            periphs.erase(periphs.begin() + st.sel_peripheral);
            st.sel_peripheral = 0;
            st.ClearTextBufs();
            st.MarkDirty();
        }
    } else {
        ImGui::TextDisabled("No peripherals defined.");
    }
    ImGui::EndChild();
}

inline void DrawJsonTab(ConfiguratorState &st) {
    ImGui::TextWrapped(
        "Raw JSON view of the whole configuration. Edit the text and press "
        "Apply to replace the document (the other tabs then show the new "
        "values). Invalid JSON is rejected and the document is kept.");
    std::string text = st.doc.dump(2);
    // Live-typed text is kept in the "__json__" buffer; Apply commits it.
    InputTextMultilineBuf(st, "##rawjson", "__json__", text,
                          ImVec2(-1, -40));
    if (ImGui::Button("Apply JSON")) {
        try {
            const nlohmann::json parsed =
                nlohmann::json::parse(st.text_bufs["__json__"]);
            st.text_no_resync.erase("__json__");
            st.doc = parsed;
            st.ClearTextBufs();
            st.MarkDirty();
            st.action_msg = "Applied edited JSON.";
        } catch (const std::exception &e) {
            st.action_msg = std::string("Invalid JSON: ") + e.what();
            st.text_no_resync.insert("__json__");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from document")) {
        st.text_no_resync.erase("__json__");
        st.text_bufs.erase("__json__");
    }
}

inline void DrawTopBar(ConfiguratorState &st) {
    ImGui::Text("File:");
    ImGui::SameLine();
    {
        std::string path_value = st.path;
        if (InputTextBuf(st, "##path", "path", path_value))
            st.path = path_value;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        if (st.dirty) {
            // Loading discards unsaved edits; the status line says so.
            st.action_msg = "Discarded unsaved changes and loaded the file.";
        }
        st.LoadFromFile(st.path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (st.path.empty())
            st.action_msg = "No file path set. Type a path above first.";
        else
            st.SaveToFile(st.path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As")) {
        if (st.path.empty())
            st.action_msg = "No file path set. Type a path above first.";
        else
            st.SaveToFile(st.path);
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        st.LoadTemplate();
    }

    ImGui::Text("Presets:");
    ImGui::SameLine();
    static const char *presets[] = {"tiny4.json", "8bit.json",
                                    "8bit_harvard.json", "16bit.json",
                                    "16bit_lab.json"};
    for (const char *preset : presets) {
        if (ImGui::Button(preset)) {
            st.LoadFromFile(ResolvePresetPath(preset));
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (st.valid && st.problem_count == 0)
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%s",
                           st.validation_msg.c_str());
    else
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                           st.validation_msg.c_str());
    ImGui::SameLine();
    if (st.dirty)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(unsaved "
                                                          "changes)");
    if (!st.action_msg.empty()) {
        ImGui::TextWrapped("%s", st.action_msg.c_str());
    }

    if (ImGui::Button("Export JSON")) {
        if (!st.valid) {
            st.action_msg =
                "Cannot export: the configuration is invalid (see above).";
        } else if (st.path.empty()) {
            st.action_msg =
                "Cannot export: no file path set. Type a path above first.";
        } else if (st.SaveToFile(st.path)) {
            st.action_msg = "Exported JSON to " + st.path + ".";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Launch Simulator")) {
        if (!st.valid) {
            st.action_msg =
                "Cannot launch: the configuration is invalid (see above).";
        } else if (st.path.empty()) {
            st.action_msg =
                "Cannot launch: save the configuration to a file first.";
        } else if (st.SaveToFile(st.path)) {
            AppLauncher::LaunchSiblingApp(st.argv0.c_str(), "cpu_sim",
                                          st.path, st.launch_message);
        }
    }
    if (!st.launch_message.empty()) {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", st.launch_message.c_str());
    }

    ImGui::SameLine();
    const char *themes[] = {"Modern Dark", "Retro Amber", "Green Phosphor",
                            "Flat Classic Windows"};
    int theme_idx = static_cast<int>(st.theme);
    if (ImGui::Combo("Theme", &theme_idx, themes, IM_ARRAYSIZE(themes))) {
        st.theme = static_cast<UITheme>(theme_idx);
        ApplyTheme(st.theme);
    }
}

} // namespace ConfiguratorDetail

inline void UI_Configurator(ConfiguratorState &st) {
    if (st.last_validated_version != st.doc_version)
        st.RefreshValidation();
    ImGui::Begin("CPU Configurator");
    ConfiguratorDetail::DrawTopBar(st);
    ImGui::Separator();
    if (ImGui::BeginTabBar("ConfiguratorTabs")) {
        if (ImGui::BeginTabItem("System")) {
            ConfiguratorDetail::DrawSystemTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Registers")) {
            ConfiguratorDetail::DrawRegistersTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory")) {
            ConfiguratorDetail::DrawMemoryTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ALU")) {
            ConfiguratorDetail::DrawAluTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Instructions")) {
            ConfiguratorDetail::DrawInstructionsTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Problems")) {
            ConfiguratorDetail::DrawProblemsTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Peripherals")) {
            ConfiguratorDetail::DrawPeripheralsTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("JSON")) {
            ConfiguratorDetail::DrawJsonTab(st);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
