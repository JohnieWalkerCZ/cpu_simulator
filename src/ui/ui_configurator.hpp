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
#include "instruction_preview.hpp"
#include "microop_editor.hpp"
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
    if (InputTextMultilineBuf(st, label, id, text, ImVec2(-1, 90))) {
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
    if (InputTextMultilineBuf(st, label, id, text, ImVec2(-1, 120))) {
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
    if (ImGui::Button("Add sub-register")) {
        if (!reg.contains("sub_registers"))
            reg["sub_registers"] = nlohmann::json::array();
        reg["sub_registers"].push_back(
            {{"name", "SUB"}, {"width", 1}, {"offset", 0}});
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
                                      prog_text, ImVec2(-1, 150))) {
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
    std::vector<std::string> items = options;
    if (literal_entry)
        items.push_back("# (literal...)");
    items.push_back("Custom...");
    int idx = static_cast<int>(items.size()) - 1; // "Custom..." default
    for (int i = 0; i < static_cast<int>(options.size()); ++i)
        if (options[i] == cur) {
            idx = i;
            break;
        }
    std::vector<const char *> citems;
    for (const auto &s : items)
        citems.push_back(s.c_str());
    if (ImGui::Combo(spec.label.c_str(), &idx, citems.data(),
                     static_cast<int>(citems.size()))) {
        if (idx < static_cast<int>(options.size())) {
            uop[key] = options[idx];
        } else if (literal_entry &&
                   idx == static_cast<int>(options.size())) {
            uop[key] = "#0"; // editable as free text below
        } else {
            uop[key] = ""; // blank custom value, typed below
        }
        st.MarkDirty();
    }
    if (std::find(options.begin(), options.end(), cur) == options.end()) {
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
        ImGui::BeginChild("uop_list", ImVec2(220, 180), true);
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
        ImGui::BeginChild("uop_editor", ImVec2(0, 180), true);
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
            EditJsonCombo(st, "Action", uop, "action", actions, 10, "halt");
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
            ImGui::Text("on_read (AST JSON, optional):");
            EditJsonRaw(st, "##onread", r, "on_read", rbase + "/on_read");
            ImGui::Text("on_write (AST JSON, optional):");
            EditJsonRaw(st, "##onwrite", r, "on_write", rbase + "/on_write");
        }

        ImGui::Separator();
        ImGui::Text("tick_behavior (AST JSON, optional):");
        EditJsonRaw(st, "##tick", p, "tick_behavior", base + "/tick");
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
