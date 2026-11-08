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
#include "app_settings.hpp"
#include "config_diagnostics.hpp"
#include "core/config.hpp"
#include "duplicate.hpp"
#include "example_items.hpp"
#include "hook_editor.hpp"
#include "instruction_preview.hpp"
#include "microop_editor.hpp"
#include "register_layout.hpp"
#include "ui_common.hpp"
#include "undo_history.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <sstream>
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

    // Undo/redo history (Group 1): snapshots of the whole document.
    // The top snapshot always equals the current document.
    UndoHistory history;

    // Pending delete confirmation (Group 1): one shared modal for
    // every Remove button. The pending delete is resolved when the
    // user confirms, so list indices cannot go stale in between.
    enum class DeleteKind {
        None,
        Instruction,
        MicroOp,
        Register,
        SubRegister,
        Segment,
        Flag,
        AluOp,
        Peripheral,
        PeriphReg,
        HookNode,
        HookArg,
        HookElseBranch
    };
    DeleteKind pending_delete_kind = DeleteKind::None;
    int pending_delete_index = -1; // list index for the pending delete
    int pending_delete_group = -1; // register group (Register deletes)
    std::string pending_delete_path; // register path (SubRegister deletes)
    std::string pending_delete_hook_id; // hook id (hook deletes)
    HookEditor::NodePath pending_delete_node_path; // node path (hook deletes)
    std::string pending_delete_name; // shown in the modal

    // Pending unsaved-changes confirmations (Group 1).
    bool open_confirm_pending = false;
    std::string pending_open_path; // empty = New template
    bool quit_requested = false;   // SDL_QUIT seen while dirty
    bool quit_confirmed = false;   // set by the quit modal; main loop reads

    // Pending navigation from a clicked problem (Group 2): the tab to
    // switch to, consumed by UI_Configurator on the next frame.
    std::string goto_tab;

    // List filters (Group 2): name substring, case-insensitive.
    std::string filter_instructions;
    std::string filter_registers;
    std::string filter_alu;
    std::string filter_peripherals;

    // Active tab (Group 3, for Ctrl+D) and persistent UI settings
    // (Group 3/5): recent files and the UI scale.
    std::string active_tab = "System";
    AppSettings settings;
    std::string settings_path;

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

    // A drag-and-drop of a hook node recorded during rendering, applied
    // at the end of the hook editor that owns the drop target. Applying
    // it immediately would mutate the hook array while its lists are
    // being drawn and invalidate the references the drawing code holds.
    // `hook_drop_from` is the payload of the drag: "move\n<hook id>\n
    // <node path>" for a moved block, "new\n<node type>" for a palette
    // drag; `hook_drop_to_list` is the target list's path ("" is the
    // top-level list of the hook).
    bool hook_drop_pending = false;
    std::string hook_drop_hook_id;
    std::string hook_drop_from;
    std::string hook_drop_to_list;
    int hook_drop_to_index = 0;

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
        history.clear();
        history.push(doc);
        history.markSaved();
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
        history.clear();
        history.push(doc);
        history.markSaved();
        if (!settings_path.empty()) {
            AddRecent(settings, file_path);
            SaveSettings(settings_path, settings);
        }
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
        history.markSaved();
        if (!settings_path.empty()) {
            AddRecent(settings, file_path);
            SaveSettings(settings_path, settings);
        }
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

// Forward declarations: the Group 1-5 UI helpers are defined
// before DrawTopBar, but the tab renderers below use them; the
// field editors call PushUndoSnapshot when an edit commits.
inline void PushUndoSnapshot(ConfiguratorState &st);
inline void AskDelete(ConfiguratorState &st,
                      ConfiguratorState::DeleteKind kind,
                      const std::string &name, int index = -1,
                      int group = -1, const std::string &path = "",
                      const std::string &hook_id = "",
                      const HookEditor::NodePath &node_path = {});
inline bool DuplicateSelectedItem(ConfiguratorState &st);
inline bool NameMatchesFilter(const std::string &name,
                              const std::string &filter);
inline void NavigateToIssue(ConfiguratorState &st,
                            const ConfigDiagnostics::Issue &issue);
inline void HelpMarker(const char *text);
inline void InsertExamplePeripheral(ConfiguratorState &st);
inline void InsertExampleHook(ConfiguratorState &st, nlohmann::json &obj,
                              const char *key);

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
    // Text fields push once, when the edit commits (deactivation
    // after a real change), never per keystroke.
    if (ImGui::IsItemDeactivatedAfterEdit())
        PushUndoSnapshot(st);
}

inline void EditJsonWordText(ConfiguratorState &st, const char *label,
                             nlohmann::json &obj, const char *key,
                             const std::string &id) {
    std::string v = obj.contains(key) ? JsonScalarToString(obj[key]) : "0";
    if (InputTextBuf(st, label, id, v)) {
        CommitWordText(obj[key], v);
        st.MarkDirty();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        PushUndoSnapshot(st);
}

inline void EditJsonInt(ConfiguratorState &st, const char *label,
                        nlohmann::json &obj, const char *key, int def,
                        int lo, int hi, bool push_undo = true) {
    int v = obj.value(key, def);
    if (ImGui::InputInt(label, &v)) {
        v = std::clamp(v, lo, hi);
        obj[key] = v;
        st.MarkDirty();
        // Discrete widgets push right after the change (dedup makes
        // no-change frames a no-op). Compound edits pass
        // push_undo=false and push once after the whole change.
        if (push_undo)
            PushUndoSnapshot(st);
    }
}

inline void EditJsonCombo(ConfiguratorState &st, const char *label,
                          nlohmann::json &obj, const char *key,
                          const char *const *items, int count,
                          const std::string &def, bool push_undo = true) {
    const std::string cur = obj.value(key, def);
    int idx = 0;
    for (int i = 0; i < count; ++i)
        if (cur == items[i])
            idx = i;
    if (ImGui::Combo(label, &idx, items, count)) {
        obj[key] = items[idx];
        st.MarkDirty();
        if (push_undo)
            PushUndoSnapshot(st);
    }
}

inline void EditJsonBool(ConfiguratorState &st, const char *label,
                         nlohmann::json &obj, const char *key, bool def) {
    bool b = obj.value(key, def);
    if (ImGui::Checkbox(label, &b)) {
        obj[key] = b;
        st.MarkDirty();
        PushUndoSnapshot(st);
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
    if (ImGui::IsItemDeactivatedAfterEdit())
        PushUndoSnapshot(st);
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
    if (ImGui::IsItemDeactivatedAfterEdit())
        PushUndoSnapshot(st);
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
        HelpMarker("How many bits the opcode field takes in each "
                   "instruction's encoding.");
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
            PushUndoSnapshot(st);
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
            PushUndoSnapshot(st);
        }
        HelpMarker("Offset slices a fixed range of bits out of the "
                   "parent register; mask picks individual bits with a "
                   "bit pattern.");
        if (idx == 0)
            EditJsonInt(st, "Offset (bits)", reg, "offset", 0, 0, 127);
        else
            EditJsonString(st, "Mask (e.g. 0b0101)", reg, "mask",
                           path + "/mask");
    }
    if (reg.contains("sub_registers")) {
        ImGui::Separator();
        ImGui::Text("Sub-registers:");
        for (size_t i = 0; i < reg["sub_registers"].size(); ++i) {
            nlohmann::json &child = reg["sub_registers"][i];
            const std::string child_path =
                path + "/sub/" + std::to_string(i);
            const std::string label =
                child.value("name", "sub") + "###sub" + std::to_string(i);
            if (ImGui::TreeNode(label.c_str())) {
                DrawRegisterEditor(st, child, false, child_path, depth + 1);
                if (ImGui::SmallButton("Remove this sub-register"))
                    AskDelete(
                        st, ConfiguratorState::DeleteKind::SubRegister,
                        child.value("name", "sub"), static_cast<int>(i),
                        -1, path);
                ImGui::TreePop();
            }
        }
        // (Sub-register deletes go through the shared confirmation
        // modal now; see AskDelete above. The actual erase happens in
        // ApplyPendingDelete once the user confirms.)
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
        PushUndoSnapshot(st);
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
    size_t reg_shown = 0, reg_total = 0;
    for (int g = 0; g < 2; ++g) {
        const nlohmann::json &arr = doc["registers"][groups[g]];
        reg_total += arr.size();
        for (size_t i = 0; i < arr.size(); ++i)
            if (NameMatchesFilter(arr[i].value("name", "?"),
                                  st.filter_registers))
                ++reg_shown;
    }
    ImGui::TextDisabled("Filter by name:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    InputTextBuf(st, "##filter_reg", "filter/registers",
                 st.filter_registers);
    ImGui::SameLine();
    ImGui::Text("%zu of %zu", reg_shown, reg_total);
    for (int g = 0; g < 2; ++g) {
        ImGui::Text("%s:", group_labels[g]);
        nlohmann::json &arr = doc["registers"][groups[g]];
        for (size_t i = 0; i < arr.size(); ++i) {
            if (!NameMatchesFilter(arr[i].value("name", "?"),
                                   st.filter_registers))
                continue;
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
            PushUndoSnapshot(st);
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
            AskDelete(st, ConfiguratorState::DeleteKind::Register,
                      reg.value("name", "?"), st.sel_register, st.sel_group);
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            DuplicateSelectedItem(st);
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
        HelpMarker("Von Neumann shares one memory for code and data; "
                   "Harvard keeps them separate.");
    }
    {
        static const char *ends[] = {"little", "big"};
        EditJsonCombo(st, "Endianness", mem, "endianness", ends, 2, "little");
        HelpMarker("Little-endian stores the lowest byte first; "
                   "big-endian stores the highest byte first.");
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
        PushUndoSnapshot(st);
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
            AskDelete(st, ConfiguratorState::DeleteKind::Segment,
                      seg.value("name", "?"), st.sel_segment);
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
        PushUndoSnapshot(st);
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
        HelpMarker("An expression over result, a, b or width that sets "
                   "this flag, e.g. result == 0; empty means the loader "
                   "decides.");
        if (ImGui::Button("Remove this flag")) {
            AskDelete(st, ConfiguratorState::DeleteKind::Flag,
                      flag.value("name", "?"), st.sel_flag);
        }
    } else {
        ImGui::TextDisabled("No flags defined.");
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("Operations:");
    ImGui::BeginChild("op_list", ImVec2(180, 0), true);
    size_t op_shown = 0;
    for (size_t i = 0; i < alu["operations"].size(); ++i)
        if (NameMatchesFilter(alu["operations"][i].value("name", "?"),
                              st.filter_alu))
            ++op_shown;
    ImGui::TextDisabled("Filter by name:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    InputTextBuf(st, "##filter_alu", "filter/alu", st.filter_alu);
    ImGui::SameLine();
    ImGui::Text("%zu of %zu", op_shown, alu["operations"].size());
    for (size_t i = 0; i < alu["operations"].size(); ++i) {
        if (!NameMatchesFilter(alu["operations"][i].value("name", "?"),
                               st.filter_alu))
            continue;
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
            PushUndoSnapshot(st);
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
                PushUndoSnapshot(st);
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
                PushUndoSnapshot(st);
            }
        }
        HelpMarker("Expression is one line like a + b; Program is a "
                   "mini-language with if, while and for, one statement "
                   "per line.");
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
            if (ImGui::IsItemDeactivatedAfterEdit())
                PushUndoSnapshot(st);
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
            AskDelete(st, ConfiguratorState::DeleteKind::AluOp,
                      op.value("name", "?"), st.sel_alu_op);
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            DuplicateSelectedItem(st);
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
inline void DrawInstructionProblems(ConfiguratorState &st,
                                    const nlohmann::json &doc,
                                    const nlohmann::json &inst) {
    const std::vector<ConfigDiagnostics::Issue> issues =
        ConfigDiagnostics::DiagnoseInstruction(doc, inst);
    ImGui::Text("Problems:");
    if (issues.empty()) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
                           "No problems in this instruction.");
        return;
    }
    int row = 0;
    for (const auto &issue : issues) {
        const std::string row_label =
            issue.problem + "###instprob" + std::to_string(row++);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Selectable(row_label.c_str(), false))
            NavigateToIssue(st, issue);
        ImGui::PopStyleColor();
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
    int row = 0; // shared across the area loop and the "Other" loop,
                 // so the ###prob ids stay unique
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
            // Clickable row: the whole where + problem line navigates
            // to the issue. (Selectable does not wrap, so a long
            // problem runs past the right edge on one line; the Fix
            // line below still wraps.)
            const std::string row_label =
                issue.where + ": " + issue.problem + "###prob" +
                std::to_string(row++);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Selectable(row_label.c_str(), false))
                NavigateToIssue(st, issue);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                std::string tip = "Go to " + issue.nav_tab;
                if (!issue.nav_name.empty())
                    tip += " / " + issue.nav_name;
                ImGui::SetTooltip("%s", tip.c_str());
            }
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
        const std::string row_label =
            issue.where + ": " + issue.problem + "###prob" +
            std::to_string(row++);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        if (ImGui::Selectable(row_label.c_str(), false))
            NavigateToIssue(st, issue);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            std::string tip = "Go to " + issue.nav_tab;
            if (!issue.nav_name.empty())
                tip += " / " + issue.nav_name;
            ImGui::SetTooltip("%s", tip.c_str());
        }
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
        if (ImGui::IsItemDeactivatedAfterEdit())
            PushUndoSnapshot(st);
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

    ImGui::BeginChild("inst_list", ImVec2(280, 0), true);
    size_t inst_shown = 0;
    for (size_t i = 0; i < insts.size(); ++i)
        if (NameMatchesFilter(insts[i].value("name", "?"),
                              st.filter_instructions))
            ++inst_shown;
    ImGui::TextDisabled("Filter by name:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    InputTextBuf(st, "##filter_inst", "filter/instructions",
                 st.filter_instructions);
    ImGui::SameLine();
    ImGui::Text("%zu of %zu", inst_shown, insts.size());
    for (size_t i = 0; i < insts.size(); ++i) {
        if (!NameMatchesFilter(insts[i].value("name", "?"),
                               st.filter_instructions))
            continue; // hidden by the filter; `i` still indexes the
                      // underlying array, so selection is unaffected
        const size_t uop_count =
            insts[i].contains("microcode") &&
                    insts[i]["microcode"].is_array()
                ? insts[i]["microcode"].size()
                : 0;
        const std::string label =
            insts[i].value("name", "?") + " (opcode " +
            std::to_string(insts[i].value("opcode", 0)) + ", " +
            std::to_string(uop_count) + " uops)" + "###inst" +
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
            PushUndoSnapshot(st);
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
        EditJsonInt(st, "Opcode", inst, "opcode", 0, 0, 65535, false);
        if (inst.value("opcode", 0) != old_opcode) {
            SyncEncodingWithOpcode(inst, inst.value("opcode", 0));
            st.ClearTextBufs();
            PushUndoSnapshot(st);
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
                PushUndoSnapshot(st);
            }
        }
        {
            static const char *modes[] = {"dynamic", "strict", "bottleneck",
                                          "additive"};
            EditJsonCombo(st, "Latency mode", inst, "latency_mode", modes, 4,
                          "dynamic");
            HelpMarker("How the instruction's latency combines with its "
                       "micro-ops: dynamic adds them up, strict uses the "
                       "latency as-is, bottleneck takes the slower of "
                       "the two, additive adds the latency on top.");
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
            if (ImGui::IsItemDeactivatedAfterEdit())
                PushUndoSnapshot(st);
            HelpMarker("A comma-separated list: the first element is the "
                       "opcode, then one element per operand, e.g. "
                       "5, dest, src.");
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
            PushUndoSnapshot(st);
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
                          "halt", false);
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
                PushUndoSnapshot(st);
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
                AskDelete(st, ConfiguratorState::DeleteKind::MicroOp,
                          uop.value("action", "?"), st.sel_microop);
            }
        } else {
            ImGui::TextDisabled("No micro-ops. An instruction needs at "
                                "least one.");
        }
        ImGui::EndChild();
        ImGui::Separator();
        DrawInstructionPreview(doc, inst);
        ImGui::Separator();
        DrawInstructionProblems(st, doc, inst);
        ImGui::Separator();
        if (ImGui::Button("Remove this instruction")) {
            AskDelete(st, ConfiguratorState::DeleteKind::Instruction,
                      inst.value("name", "?"), st.sel_instruction);
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            DuplicateSelectedItem(st);
        }
    } else {
        ImGui::TextDisabled("No instructions defined.");
    }
    ImGui::EndChild();
}

} // namespace ConfiguratorDetail

namespace ConfiguratorDetail {

// Scratch-style block editor for one peripheral hook (on_read,
// on_write or tick_behavior): a JSON array of AST nodes. Each node is
// a coloured, rounded block (if = orange, assign = blue, call =
// green); `if` blocks are C-shaped, their then/else lists render
// indented inside the parent block. Blocks are draggable by their
// header: reorder within a list, or drop into another list (top
// level, a then-list, an else-list); a palette at the top of each
// hook editor drags in new blocks. The drag-and-drop model (paths,
// MoveNode, InsertNode) lives in hook_editor.hpp and is unit-tested;
// the UI only records a drop in ConfiguratorState and DrawHookEditor
// applies it after drawing, so the hook array is never mutated while
// its lists are being drawn.
//
// ID safety (this used to crash with ImGui's "3 visible items with
// conflicting ID!" popup): every hook editor PushIDs its hook id, so
// the three hooks of one peripheral never share a scope, and every
// node list PushIDs its path id, so a node 0 in a then-list and a
// node 0 in an else-list under the same parent are distinct too.
// Every repeated label inside ("##type", "##up", "##down", "##drag",
// "##drop_end", "+ Add node", ...) is therefore scoped by hook +
// list + index. Text view is the existing EditJsonRaw pretty-printed
// box; if the typed text does not parse, the text is kept, the error
// is shown, and Blocks stays disabled until the JSON parses again. A
// node with an unknown type is preserved as-is and shown greyed-out
// with a hint to use Text mode.

inline void DrawHookNodeList(ConfiguratorState &st,
                             const nlohmann::json &periph,
                             nlohmann::json &nodes, const std::string &id,
                             const HookEditor::NodePath &list_path,
                             const std::string &hook_id);
inline void DrawHookNode(ConfiguratorState &st,
                         const nlohmann::json &periph, nlohmann::json &node,
                         const std::string &id,
                         const HookEditor::NodePath &node_path,
                         const std::string &hook_id, int &erase_out,
                         int &move_out);

// Scratch-style colours per node type: the block background tint and
// the header text colour. Unknown types get a grey block.
inline void HookBlockColors(const std::string &type, ImVec4 &bg,
                            ImVec4 &header) {
    if (type == "if") {
        bg = ImVec4(0.42f, 0.26f, 0.08f, 1.0f);
        header = ImVec4(1.0f, 0.65f, 0.25f, 1.0f);
    } else if (type == "call") {
        bg = ImVec4(0.08f, 0.36f, 0.16f, 1.0f);
        header = ImVec4(0.45f, 0.95f, 0.55f, 1.0f);
    } else if (type == "assign") {
        bg = ImVec4(0.10f, 0.24f, 0.46f, 1.0f);
        header = ImVec4(0.55f, 0.80f, 1.0f, 1.0f);
    } else {
        bg = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        header = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
    }
}

// Parse a drag payload: "move\n<hook id>\n<node path>" for a moved
// block, "new\n<node type>" for a palette drag. Returns false for
// anything that is not a hook-node drag.
inline bool ParseHookDragPayload(const std::string &data, bool &is_new,
                                 std::string &hook_id,
                                 std::string &path_or_type) {
    const size_t nl1 = data.find('\n');
    if (nl1 == std::string::npos)
        return false;
    const std::string kind = data.substr(0, nl1);
    if (kind == "new") {
        is_new = true;
        hook_id.clear();
        path_or_type = data.substr(nl1 + 1);
        return !path_or_type.empty();
    }
    if (kind != "move")
        return false;
    const size_t nl2 = data.find('\n', nl1 + 1);
    if (nl2 == std::string::npos)
        return false;
    is_new = false;
    hook_id = data.substr(nl1 + 1, nl2 - nl1 - 1);
    path_or_type = data.substr(nl2 + 1);
    return !hook_id.empty() && !path_or_type.empty();
}

// Call right after the item that starts a hook-node drag (a block's
// header handle, or a palette button). `payload` is the full payload
// string; `label` is the text shown in the drag preview.
inline void HookDragSource(const std::string &payload, const char *label) {
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("HOOK_NODE", payload.c_str(),
                                  payload.size() + 1);
        ImGui::Text("%s", label);
        ImGui::EndDragDropSource();
    }
}

// Call right after the item that should accept hook-node drops (a
// block, after EndChild, or a list's trailing drop zone). `index` is
// the position a drop inserts at; when `split` is true the mouse
// being in the lower half of the item moves the position one down, so
// dropping on a block inserts before or after it. While a payload
// hovers the item an insertion line is drawn at the effective edge.
// Drops from a different hook, and moves into the dragged node's own
// descendant, are not accepted at all. A drop is only recorded (into
// ConfiguratorState, applied by DrawHookEditor after drawing);
// nothing is mutated here. ImGui accepts the smallest overlapping
// target, so a nested block wins over the block it sits in.
inline void HookDropTarget(ConfiguratorState &st,
                           const std::string &hook_id,
                           const HookEditor::NodePath &list_path, int index,
                           bool split) {
    if (!ImGui::BeginDragDropTarget())
        return;
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const bool lower_half =
        split && ImGui::GetMousePos().y > (mn.y + mx.y) * 0.5f;
    const int at = lower_half ? index + 1 : index;
    const ImGuiPayload *hover = ImGui::GetDragDropPayload();
    bool acceptable = false;
    if (hover != nullptr && hover->IsDataType("HOOK_NODE")) {
        const std::string data(static_cast<const char *>(hover->Data));
        bool is_new = false;
        std::string src_hook, src_path;
        if (ParseHookDragPayload(data, is_new, src_hook, src_path)) {
            if (is_new) {
                acceptable = true;
            } else if (src_hook == hook_id) {
                HookEditor::NodePath from;
                acceptable =
                    HookEditor::ParsePath(src_path, from) &&
                    !HookEditor::MoveIsSelfTarget(from, list_path);
            }
        }
    }
    if (acceptable) {
        // AcceptBeforeDelivery makes AcceptDragDropPayload return the
        // payload while the mouse is still down, so the line shows
        // only for the smallest (winning) target; the drop is
        // recorded only on the delivery frame. The default target
        // rectangle is suppressed: the insertion line is the visual.
        if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload(
                "HOOK_NODE",
                ImGuiDragDropFlags_AcceptBeforeDelivery |
                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            const float y = lower_half ? mx.y - 2.0f : mn.y + 2.0f;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(mn.x, y), ImVec2(mx.x, y), IM_COL32(255, 200, 40, 255),
                2.0f);
            if (pl->Delivery) {
                st.hook_drop_pending = true;
                st.hook_drop_hook_id = hook_id;
                st.hook_drop_from =
                    std::string(static_cast<const char *>(pl->Data));
                st.hook_drop_to_list = HookEditor::PathToString(list_path);
                st.hook_drop_to_index = at;
            }
        }
    }
    ImGui::EndDragDropTarget();
}

// One hook: Blocks/Text toggle, a hint about the `value` context
// variable, then either the node blocks or the raw JSON box. The
// whole editor is scoped by the hook id (see the header comment):
// this is what stops the three hooks of one peripheral from
// colliding on the toggle, "+ Add node", and node labels.
inline void DrawHookEditor(ConfiguratorState &st,
                           const nlohmann::json &periph, nlohmann::json &obj,
                           const char *key, const std::string &id,
                           const char *value_hint) {
    ImGui::PushID(id.c_str());
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
        text_view
            ? "Text view: the hook as pretty-printed JSON."
            : "Blocks view: drag a block by its header to move it, "
              "or drag one in from the palette.");
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
    } else if (!obj.contains(key)) {
        ImGui::TextDisabled("No nodes yet.");
        if (ImGui::SmallButton("+ Add node")) {
            obj[key] = nlohmann::json::array(
                {HookEditor::DefaultNode(
                    "assign", HookEditor::AvailableTargets(periph))});
            st.ClearTextBufs();
            st.MarkDirty();
            PushUndoSnapshot(st);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Insert example hook"))
            InsertExampleHook(st, obj, key);
    } else {
        // Palette: drag a type onto any list, or click to append a
        // node of that type to this hook's top-level list.
        ImGui::Text("Palette (drag onto a list, or click to append):");
        for (const std::string &type : HookEditor::NodeTypes()) {
            ImVec4 bg, header_col;
            HookBlockColors(type, bg, header_col);
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, header_col);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, header_col);
            if (ImGui::SmallButton(type.c_str())) {
                obj[key].push_back(HookEditor::DefaultNode(
                    type, HookEditor::AvailableTargets(periph)));
                st.ClearTextBufs();
                st.MarkDirty();
                PushUndoSnapshot(st);
            }
            ImGui::PopStyleColor(3);
            HookDragSource("new\n" + type, (type + " block").c_str());
            ImGui::SameLine();
        }
        ImGui::NewLine();
        DrawHookNodeList(st, periph, obj[key], id, {}, id);
        // Apply a drag-and-drop recorded while the list was drawing.
        // The hook array must not be mutated while its lists are
        // being drawn, so the drop is recorded in ConfiguratorState
        // and applied here, once drawing is done.
        if (st.hook_drop_pending && st.hook_drop_hook_id == id) {
            st.hook_drop_pending = false;
            bool ok = false;
            bool is_new = false;
            std::string src_hook, src_path;
            if (ParseHookDragPayload(st.hook_drop_from, is_new, src_hook,
                                     src_path)) {
                HookEditor::NodePath to_list;
                if (HookEditor::ParsePath(st.hook_drop_to_list, to_list)) {
                    if (is_new) {
                        ok = HookEditor::InsertNode(
                            obj[key], to_list, st.hook_drop_to_index,
                            HookEditor::DefaultNode(
                                src_path,
                                HookEditor::AvailableTargets(periph)));
                    } else if (src_hook == id) {
                        HookEditor::NodePath from;
                        if (HookEditor::ParsePath(src_path, from))
                            ok = HookEditor::MoveNode(
                                obj[key], from, to_list,
                                st.hook_drop_to_index);
                    }
                }
            }
            if (ok) {
                st.ClearTextBufs();
                st.MarkDirty();
                PushUndoSnapshot(st);
            }
        }
        if (ImGui::SmallButton("Insert example hook"))
            InsertExampleHook(st, obj, key);
    }
    ImGui::PopID();
}

// A vertical list of node blocks, each with move-up / move-down /
// remove, a trailing drop zone (dropping there appends), and a
// "+ Add node" button (new node defaults to assign). The list scopes
// itself by its path id, so a node 0 in a then-list and a node 0 in
// an else-list under the same parent never share an ImGui ID.
inline void DrawHookNodeList(ConfiguratorState &st,
                             const nlohmann::json &periph,
                             nlohmann::json &nodes, const std::string &id,
                             const HookEditor::NodePath &list_path,
                             const std::string &hook_id) {
    ImGui::PushID(id.c_str());
    int erase_idx = -1, move_idx = -1, move_dir = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        nlohmann::json &node = nodes[i];
        ImGui::PushID(static_cast<int>(i));
        int erase = 0, move = 0;
        HookEditor::NodePath node_path = list_path;
        node_path.push_back(HookEditor::PathSeg::Index(static_cast<int>(i)));
        DrawHookNode(st, periph, node, id + "/" + std::to_string(i),
                     node_path, hook_id, erase, move);
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
        HookEditor::NodePath node_path = list_path;
        node_path.push_back(HookEditor::PathSeg::Index(erase_idx));
        AskDelete(st, ConfiguratorState::DeleteKind::HookNode,
                  nodes[erase_idx].value("type", "?"), erase_idx, -1, "",
                  hook_id, node_path);
    } else if (move_idx >= 0) {
        const int j = move_idx + move_dir;
        if (j >= 0 && j < static_cast<int>(nodes.size())) {
            std::swap(nodes[move_idx], nodes[j]);
            st.ClearTextBufs();
            st.MarkDirty();
            PushUndoSnapshot(st);
        }
    }
    // Trailing drop zone: dropping here appends to this list.
    const float zone_h = nodes.empty() ? 36.0f : 10.0f;
    ImGui::InvisibleButton("##drop_end",
                           ImVec2(ImGui::GetContentRegionAvail().x, zone_h));
    if (nodes.empty()) {
        const ImVec2 mn = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(mn.x + 6.0f, mn.y + 8.0f), IM_COL32(160, 160, 160, 255),
            "Drop a block here, or use + Add node below.");
    }
    HookDropTarget(st, hook_id, list_path,
                   static_cast<int>(nodes.size()), false);
    if (ImGui::SmallButton("+ Add node")) {
        nodes.push_back(HookEditor::DefaultNode(
            "assign", HookEditor::AvailableTargets(periph)));
        st.ClearTextBufs();
        st.MarkDirty();
        PushUndoSnapshot(st);
    }
    ImGui::PopID();
}

// One node as a Scratch-style block: a rounded, coloured child window
// with a header (type, drag handle, move-up / move-down / remove) and
// its fields inline. `if` blocks are C-shaped: their then/else node
// lists render indented inside the parent block, with an "else:"
// divider. The whole block is a drop target: dropping on its upper
// half inserts before it, on its lower half after it (the smallest
// overlapping target wins, so a nested block wins over the block it
// sits in). Changing the type rebuilds the node with that type's
// defaults; unknown types are preserved and shown greyed-out.
inline void DrawHookNode(ConfiguratorState &st,
                         const nlohmann::json &periph, nlohmann::json &node,
                         const std::string &id,
                         const HookEditor::NodePath &node_path,
                         const std::string &hook_id, int &erase_out,
                         int &move_out) {
    erase_out = 0;
    move_out = 0;
    const bool known = HookEditor::IsRepresentable(node);
    const std::string cur_type =
        node.is_object() ? node.value("type", "?") : "?";
    ImVec4 bg, header_col;
    HookBlockColors(known ? cur_type : "?", bg, header_col);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(header_col.x, header_col.y, header_col.z, 0.55f));
    ImGui::BeginChild("##block", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    // Header: type, drag handle, move-up / move-down / remove.
    bool type_changed = false;
    if (known) {
        const std::vector<std::string> types = HookEditor::NodeTypes();
        int idx = 1; // assign
        for (int i = 0; i < static_cast<int>(types.size()); ++i)
            if (types[i] == cur_type)
                idx = i;
        std::vector<const char *> citems;
        for (const auto &t : types)
            citems.push_back(t.c_str());
        ImGui::SetNextItemWidth(96.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, header_col);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, header_col);
        if (ImGui::Combo("##type", &idx, citems.data(),
                         static_cast<int>(citems.size()))) {
            node = HookEditor::DefaultNode(
                types[idx], HookEditor::AvailableTargets(periph));
            st.ClearTextBufs();
            st.MarkDirty();
            PushUndoSnapshot(st);
            type_changed = true;
        }
        ImGui::PopStyleColor(3);
    } else {
        ImGui::TextColored(header_col, "unknown");
    }
    ImGui::SameLine();
    const float fh = ImGui::GetFrameHeight();
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float xw = ImGui::CalcTextSize("x").x +
                     2.0f * ImGui::GetStyle().FramePadding.x;
    float drag_w = ImGui::GetContentRegionAvail().x -
                   (2.0f * (fh + sp) + (xw + sp));
    if (drag_w < 24.0f)
        drag_w = 24.0f;
    ImGui::InvisibleButton("##drag",
                           ImVec2(drag_w, ImGui::GetTextLineHeight()));
    HookDragSource("move\n" + hook_id + "\n" +
                       HookEditor::PathToString(node_path),
                   (cur_type + " block").c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drag to move this block");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        move_out = -1;
    ImGui::SameLine();
    if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        move_out = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("x"))
        erase_out = 1;
    if (!type_changed) {
        if (!known) {
            ImGui::TextDisabled("Unknown node type -- edit in Text mode:");
            ImGui::TextWrapped("%s", node.dump(2).c_str());
        } else if (cur_type == "if") {
            ImGui::Text("condition");
            std::string cond = node.value("condition", "1");
            if (InputTextBuf(st, "##condition", id + "/condition", cond)) {
                node["condition"] = cond;
                st.MarkDirty();
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                PushUndoSnapshot(st);
            ImGui::Text("then:");
            ImGui::Indent();
            HookEditor::NodePath then_path = node_path;
            then_path.push_back(HookEditor::PathSeg::Key("then"));
            DrawHookNodeList(st, periph, node["then"], id + "/then",
                             then_path, hook_id);
            ImGui::Unindent();
            if (node.contains("else")) {
                ImGui::Text("else:");
                ImGui::Indent();
                HookEditor::NodePath else_path = node_path;
                else_path.push_back(HookEditor::PathSeg::Key("else"));
                DrawHookNodeList(st, periph, node["else"], id + "/else",
                                 else_path, hook_id);
                ImGui::Unindent();
                if (ImGui::SmallButton("Remove else branch")) {
                    AskDelete(st,
                              ConfiguratorState::DeleteKind::HookElseBranch,
                              "else branch", -1, -1, "", hook_id, node_path);
                }
            } else if (ImGui::SmallButton("+ Add else branch")) {
                node["else"] = nlohmann::json::array();
                st.ClearTextBufs();
                st.MarkDirty();
                PushUndoSnapshot(st);
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
                PushUndoSnapshot(st);
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
                if (ImGui::IsItemDeactivatedAfterEdit())
                    PushUndoSnapshot(st);
            }
            ImGui::Text("expr");
            std::string expr = node.value("expr", "0");
            if (InputTextBuf(st, "##expr", id + "/expr", expr)) {
                node["expr"] = expr;
                st.MarkDirty();
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                PushUndoSnapshot(st);
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
                PushUndoSnapshot(st);
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
                if (ImGui::IsItemDeactivatedAfterEdit())
                    PushUndoSnapshot(st);
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
                if (ImGui::IsItemDeactivatedAfterEdit())
                    PushUndoSnapshot(st);
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                    erase_arg = static_cast<int>(j);
                ImGui::PopID();
            }
            if (erase_arg >= 0) {
                AskDelete(st, ConfiguratorState::DeleteKind::HookArg,
                          "argument " + std::to_string(erase_arg + 1),
                          erase_arg, -1, "", hook_id, node_path);
            }
            if (ImGui::SmallButton("+ Add argument")) {
                if (!node.contains("args") || !node["args"].is_array())
                    node["args"] = nlohmann::json::array({"0"});
                else
                    node["args"].push_back("0");
                st.ClearTextBufs();
                st.MarkDirty();
                PushUndoSnapshot(st);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    // The whole block is a drop target (see HookDropTarget).
    HookDropTarget(st, hook_id, HookEditor::ParentPath(node_path),
                   node_path.back().index, true);
}


inline void DrawPeripheralsTab(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    if (!doc.contains("peripherals") || !doc["peripherals"].is_array())
        doc["peripherals"] = nlohmann::json::array();
    nlohmann::json &periphs = doc["peripherals"];

    ImGui::BeginChild("periph_list", ImVec2(200, 0), true);
    size_t per_shown = 0;
    for (size_t i = 0; i < periphs.size(); ++i)
        if (NameMatchesFilter(periphs[i].value("name", "?"),
                              st.filter_peripherals))
            ++per_shown;
    ImGui::TextDisabled("Filter by name:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
    InputTextBuf(st, "##filter_per", "filter/peripherals",
                 st.filter_peripherals);
    ImGui::SameLine();
    ImGui::Text("%zu of %zu", per_shown, periphs.size());
    for (size_t i = 0; i < periphs.size(); ++i) {
        if (!NameMatchesFilter(periphs[i].value("name", "?"),
                               st.filter_peripherals))
            continue;
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
        PushUndoSnapshot(st);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Insert example peripheral"))
        InsertExamplePeripheral(st);
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
            PushUndoSnapshot(st);
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
                AskDelete(st, ConfiguratorState::DeleteKind::PeriphReg,
                          r.value("name", "?"), st.sel_periph_reg);
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
            AskDelete(st, ConfiguratorState::DeleteKind::Peripheral,
                      p.value("name", "?"), st.sel_peripheral);
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            DuplicateSelectedItem(st);
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
            PushUndoSnapshot(st);
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

// ---- Group 1 (Safety): undo/redo, delete confirmation, unsaved
// ---- changes warnings. The snapshot rule: push the document AFTER
// ---- a mutation (UndoHistory owns the current state, so undo can
// ---- put it on the redo stack); identical consecutive snapshots
// ---- are deduped inside push(), so unconditional pushes are safe.

inline void PushUndoSnapshot(ConfiguratorState &st) {
    st.history.push(st.doc);
}

inline void DoUndo(ConfiguratorState &st) {
    const std::optional<nlohmann::json> prev = st.history.undo();
    if (!prev.has_value())
        return;
    st.doc = *prev;
    st.dirty = !st.history.isSaved(st.doc);
    ++st.doc_version;
    st.ClearTextBufs();
}

inline void DoRedo(ConfiguratorState &st) {
    const std::optional<nlohmann::json> next = st.history.redo();
    if (!next.has_value())
        return;
    st.doc = *next;
    st.dirty = !st.history.isSaved(st.doc);
    ++st.doc_version;
    st.ClearTextBufs();
}

// Resolve a register by its editor path ("general_purpose/0",
// "special/2", optionally "/sub/<i>" repeated for nested
// sub-registers). Used to find a sub-register's parent when its
// delete is confirmed. Returns nullptr when the path is stale.
inline nlohmann::json *ResolveRegisterByPath(ConfiguratorState &st,
                                             const std::string &path) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= path.size()) {
        const size_t slash = path.find('/', pos);
        parts.push_back(path.substr(
            pos, slash == std::string::npos ? std::string::npos
                                            : slash - pos));
        if (slash == std::string::npos)
            break;
        pos = slash + 1;
    }
    if (parts.size() < 2 || !st.doc.contains("registers") ||
        !st.doc["registers"].is_object())
        return nullptr;
    nlohmann::json *cur = nullptr;
    try {
        const int idx = std::stoi(parts[1]);
        nlohmann::json &arr = st.doc["registers"][parts[0]];
        if (!arr.is_array() || idx < 0 ||
            idx >= static_cast<int>(arr.size()))
            return nullptr;
        cur = &arr[static_cast<size_t>(idx)];
        size_t i = 2;
        while (i + 1 < parts.size()) {
            if (parts[i] != "sub")
                return nullptr;
            const int sub = std::stoi(parts[i + 1]);
            if (!cur->contains("sub_registers") ||
                !(*cur)["sub_registers"].is_array() || sub < 0 ||
                sub >= static_cast<int>((*cur)["sub_registers"].size()))
                return nullptr;
            cur = &(*cur)["sub_registers"][static_cast<size_t>(sub)];
            i += 2;
        }
    } catch (...) {
        return nullptr;
    }
    return cur;
}

// Resolve a hook array by its editor id ("per/<p>/tick" or
// "per/<p>/preg/<r>/on_read|on_write"). Returns nullptr when stale.
inline nlohmann::json *ResolveHookArray(ConfiguratorState &st,
                                        const std::string &hook_id) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= hook_id.size()) {
        const size_t slash = hook_id.find('/', pos);
        parts.push_back(hook_id.substr(
            pos, slash == std::string::npos ? std::string::npos
                                            : slash - pos));
        if (slash == std::string::npos)
            break;
        pos = slash + 1;
    }
    if (parts.size() < 3 || parts[0] != "per" ||
        !st.doc.contains("peripherals") ||
        !st.doc["peripherals"].is_array())
        return nullptr;
    try {
        const int p = std::stoi(parts[1]);
        nlohmann::json &periphs = st.doc["peripherals"];
        if (p < 0 || p >= static_cast<int>(periphs.size()))
            return nullptr;
        nlohmann::json &periph = periphs[static_cast<size_t>(p)];
        if (parts.size() == 3 && parts[2] == "tick") {
            if (!periph.contains("tick_behavior"))
                return nullptr;
            return &periph["tick_behavior"];
        }
        if (parts.size() == 5 && parts[2] == "preg") {
            const int r = std::stoi(parts[3]);
            if (!periph.contains("registers") ||
                !periph["registers"].is_array())
                return nullptr;
            nlohmann::json &regs = periph["registers"];
            if (r < 0 || r >= static_cast<int>(regs.size()))
                return nullptr;
            nlohmann::json &reg = regs[static_cast<size_t>(r)];
            if (!reg.contains(parts[4]))
                return nullptr;
            return &reg[parts[4]];
        }
    } catch (...) {
        return nullptr;
    }
    return nullptr;
}

// Record a pending delete; the shared modal asks for confirmation
// and ApplyPendingDelete resolves it. `path` is the register path
// for SubRegister deletes, `hook_id` + `node_path` identify hook
// deletes; the rest use `index` (+ `group` for registers).
inline void AskDelete(ConfiguratorState &st,
                      ConfiguratorState::DeleteKind kind,
                      const std::string &name, int index, int group,
                      const std::string &path,
                      const std::string &hook_id,
                      const HookEditor::NodePath &node_path) {
    st.pending_delete_kind = kind;
    st.pending_delete_name = name;
    st.pending_delete_index = index;
    st.pending_delete_group = group;
    st.pending_delete_path = path;
    st.pending_delete_hook_id = hook_id;
    st.pending_delete_node_path = node_path;
}

// Carry out the pending delete (called from the modal's Delete
// button). Every branch re-checks its bounds, so a delete recorded
// before the document changed cannot erase the wrong item. The
// snapshot is pushed after the mutation.
inline void ApplyPendingDelete(ConfiguratorState &st) {
    using Kind = ConfiguratorState::DeleteKind;
    const Kind kind = st.pending_delete_kind;
    const int idx = st.pending_delete_index;
    nlohmann::json &doc = st.doc;
    bool done = false;
    switch (kind) {
        case Kind::Instruction: {
            if (!doc.contains("instruction_set") ||
                !doc["instruction_set"].contains("instructions") ||
                !doc["instruction_set"]["instructions"].is_array())
                break;
            nlohmann::json &insts =
                doc["instruction_set"]["instructions"];
            if (idx < 0 || idx >= static_cast<int>(insts.size()))
                break;
            insts.erase(insts.begin() + idx);
            st.sel_instruction = 0;
            done = true;
            break;
        }
        case Kind::MicroOp: {
            if (!doc.contains("instruction_set") ||
                !doc["instruction_set"].contains("instructions") ||
                !doc["instruction_set"]["instructions"].is_array())
                break;
            nlohmann::json &insts =
                doc["instruction_set"]["instructions"];
            if (st.sel_instruction < 0 ||
                st.sel_instruction >= static_cast<int>(insts.size()))
                break;
            nlohmann::json &inst = insts[st.sel_instruction];
            if (!inst.contains("microcode") ||
                !inst["microcode"].is_array())
                break;
            nlohmann::json &uops = inst["microcode"];
            if (idx < 0 || idx >= static_cast<int>(uops.size()))
                break;
            uops.erase(uops.begin() + idx);
            st.sel_microop = 0;
            done = true;
            break;
        }
        case Kind::Register: {
            const char *group = st.pending_delete_group == 0
                                    ? "general_purpose"
                                    : "special";
            if (!doc.contains("registers") ||
                !doc["registers"].contains(group))
                break;
            nlohmann::json &arr = doc["registers"][group];
            if (!arr.is_array() || idx < 0 ||
                idx >= static_cast<int>(arr.size()))
                break;
            arr.erase(arr.begin() + idx);
            st.sel_register = 0;
            done = true;
            break;
        }
        case Kind::SubRegister: {
            nlohmann::json *reg =
                ResolveRegisterByPath(st, st.pending_delete_path);
            if (!reg || !reg->contains("sub_registers") ||
                !(*reg)["sub_registers"].is_array())
                break;
            nlohmann::json &subs = (*reg)["sub_registers"];
            if (idx < 0 || idx >= static_cast<int>(subs.size()))
                break;
            subs.erase(subs.begin() + idx);
            done = true;
            break;
        }
        case Kind::Segment: {
            if (!doc.contains("memory") ||
                !doc["memory"].contains("segments") ||
                !doc["memory"]["segments"].is_array())
                break;
            nlohmann::json &segs = doc["memory"]["segments"];
            if (idx < 0 || idx >= static_cast<int>(segs.size()))
                break;
            segs.erase(segs.begin() + idx);
            st.sel_segment = 0;
            done = true;
            break;
        }
        case Kind::Flag: {
            if (!doc.contains("alu") || !doc["alu"].contains("flags") ||
                !doc["alu"]["flags"].is_array())
                break;
            nlohmann::json &flags = doc["alu"]["flags"];
            if (idx < 0 || idx >= static_cast<int>(flags.size()))
                break;
            flags.erase(flags.begin() + idx);
            st.sel_flag = 0;
            done = true;
            break;
        }
        case Kind::AluOp: {
            if (!doc.contains("alu") ||
                !doc["alu"].contains("operations") ||
                !doc["alu"]["operations"].is_array())
                break;
            nlohmann::json &ops = doc["alu"]["operations"];
            if (idx < 0 || idx >= static_cast<int>(ops.size()))
                break;
            ops.erase(ops.begin() + idx);
            st.sel_alu_op = 0;
            done = true;
            break;
        }
        case Kind::Peripheral: {
            if (!doc.contains("peripherals") ||
                !doc["peripherals"].is_array())
                break;
            nlohmann::json &periphs = doc["peripherals"];
            if (idx < 0 || idx >= static_cast<int>(periphs.size()))
                break;
            periphs.erase(periphs.begin() + idx);
            st.sel_peripheral = 0;
            done = true;
            break;
        }
        case Kind::PeriphReg: {
            if (!doc.contains("peripherals") ||
                !doc["peripherals"].is_array())
                break;
            nlohmann::json &periphs = doc["peripherals"];
            if (st.sel_peripheral < 0 ||
                st.sel_peripheral >= static_cast<int>(periphs.size()))
                break;
            nlohmann::json &periph = periphs[st.sel_peripheral];
            if (!periph.contains("registers") ||
                !periph["registers"].is_array())
                break;
            nlohmann::json &regs = periph["registers"];
            if (idx < 0 || idx >= static_cast<int>(regs.size()))
                break;
            regs.erase(regs.begin() + idx);
            st.sel_periph_reg = 0;
            done = true;
            break;
        }
        case Kind::HookNode: {
            nlohmann::json *hook =
                ResolveHookArray(st, st.pending_delete_hook_id);
            if (hook && hook->is_array() &&
                HookEditor::RemoveNode(*hook, st.pending_delete_node_path))
                done = true;
            break;
        }
        case Kind::HookArg: {
            nlohmann::json *hook =
                ResolveHookArray(st, st.pending_delete_hook_id);
            if (!hook || !hook->is_array())
                break;
            nlohmann::json *node = HookEditor::FindNode(
                *hook, st.pending_delete_node_path);
            if (!node || !node->contains("args") ||
                !(*node)["args"].is_array())
                break;
            nlohmann::json &args = (*node)["args"];
            if (idx < 0 || idx >= static_cast<int>(args.size()))
                break;
            args.erase(args.begin() + idx);
            done = true;
            break;
        }
        case Kind::HookElseBranch: {
            nlohmann::json *hook =
                ResolveHookArray(st, st.pending_delete_hook_id);
            if (!hook || !hook->is_array())
                break;
            nlohmann::json *node = HookEditor::FindNode(
                *hook, st.pending_delete_node_path);
            if (!node || !node->contains("else"))
                break;
            node->erase("else");
            done = true;
            break;
        }
        case Kind::None:
            break;
    }
    if (done) {
        st.ClearTextBufs();
        st.MarkDirty();
        PushUndoSnapshot(st);
    }
}

// Load a file (or the blank template) only after the user confirms
// when the document has unsaved changes; otherwise load at once.
inline void RequestLoad(ConfiguratorState &st,
                        const std::string &file_path) {
    if (st.dirty) {
        st.pending_open_path = file_path;
        st.open_confirm_pending = true;
        return;
    }
    st.LoadFromFile(file_path);
}

inline void RequestNewTemplate(ConfiguratorState &st) {
    if (st.dirty) {
        st.pending_open_path.clear();
        st.open_confirm_pending = true;
        return;
    }
    st.LoadTemplate();
}

// The three confirmation modals, rendered at the end of
// UI_Configurator. OpenPopup is called right before BeginPopupModal
// in the same frame, which is the documented way to open a modal.
inline void RenderDeleteModal(ConfiguratorState &st) {
    if (st.pending_delete_kind == ConfiguratorState::DeleteKind::None)
        return;
    ImGui::OpenPopup("Confirm delete");
    if (ImGui::BeginPopupModal("Confirm delete", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %s? You can undo with Ctrl+Z.",
                    st.pending_delete_name.c_str());
        if (ImGui::Button("Delete")) {
            ApplyPendingDelete(st);
            st.pending_delete_kind = ConfiguratorState::DeleteKind::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            st.pending_delete_kind = ConfiguratorState::DeleteKind::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

inline void RenderOpenConfirmModal(ConfiguratorState &st) {
    if (!st.open_confirm_pending)
        return;
    ImGui::OpenPopup("Unsaved changes");
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Discard them?");
        if (ImGui::Button("Discard")) {
            st.open_confirm_pending = false;
            ImGui::CloseCurrentPopup();
            if (st.pending_open_path.empty())
                st.LoadTemplate();
            else
                st.LoadFromFile(st.pending_open_path);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            st.open_confirm_pending = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

inline void RenderQuitModal(ConfiguratorState &st) {
    if (!st.quit_requested)
        return;
    ImGui::OpenPopup("Quit without saving?");
    if (ImGui::BeginPopupModal("Quit without saving?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Quit without saving?");
        if (ImGui::Button("Quit")) {
            st.quit_confirmed = true;
            st.quit_requested = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            st.quit_requested = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---- Group 2 (Navigation): clickable problems and list filters.

// Case-insensitive substring match for list filters; an empty filter
// matches everything.
inline bool NameMatchesFilter(const std::string &name,
                              const std::string &filter) {
    if (filter.empty())
        return true;
    auto lower = [](const std::string &s) {
        std::string out = s;
        for (char &c : out)
            c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    return lower(name).find(lower(filter)) != std::string::npos;
}

// Navigate to an issue: switch to its tab (consumed next frame by
// UI_Configurator via st.goto_tab) and select its item. nav_index -1
// switches the tab only. Registers use a flattened top-level index
// (general_purpose first, then special): split it into
// (sel_group, sel_register). The ALU tab holds two lists; flags and
// operations are told apart by the "ALU / flag ..." prefix of `where`.
// Every tab's editor clamps its sel_* on draw, so an out-of-range
// nav_index is harmless.
inline void NavigateToIssue(ConfiguratorState &st,
                            const ConfigDiagnostics::Issue &issue) {
    st.goto_tab = issue.nav_tab;
    if (issue.nav_index < 0)
        return;
    if (issue.nav_tab == "Instructions") {
        st.sel_instruction = issue.nav_index;
    } else if (issue.nav_tab == "Registers") {
        int gp_count = 0;
        if (st.doc.contains("registers") &&
            st.doc["registers"].is_object() &&
            st.doc["registers"].contains("general_purpose") &&
            st.doc["registers"]["general_purpose"].is_array())
            gp_count = static_cast<int>(
                st.doc["registers"]["general_purpose"].size());
        if (issue.nav_index < gp_count) {
            st.sel_group = 0;
            st.sel_register = issue.nav_index;
        } else {
            st.sel_group = 1;
            st.sel_register = issue.nav_index - gp_count;
        }
    } else if (issue.nav_tab == "ALU") {
        if (issue.where.rfind("ALU / flag ", 0) == 0)
            st.sel_flag = issue.nav_index;
        else
            st.sel_alu_op = issue.nav_index;
    } else if (issue.nav_tab == "Peripherals") {
        st.sel_peripheral = issue.nav_index;
        // Also select the peripheral register named in `where`
        // ("Peripherals / 'P' register 'R'"), when there is one.
        const std::string marker = " register '";
        const size_t pos = issue.where.find(marker);
        if (pos != std::string::npos) {
            const size_t begin = pos + marker.size();
            const size_t end = issue.where.find('\'', begin);
            if (end != std::string::npos &&
                st.doc.contains("peripherals") &&
                st.doc["peripherals"].is_array() &&
                issue.nav_index >= 0 &&
                issue.nav_index <
                    static_cast<int>(st.doc["peripherals"].size())) {
                const std::string reg_name =
                    issue.where.substr(begin, end - begin);
                const auto &regs =
                    st.doc["peripherals"][issue.nav_index]["registers"];
                if (regs.is_array())
                    for (size_t r = 0; r < regs.size(); ++r)
                        if (regs[r].value("name", "") == reg_name) {
                            st.sel_periph_reg = static_cast<int>(r);
                            break;
                        }
            }
        }
    } else if (issue.nav_tab == "Memory") {
        st.sel_segment = issue.nav_index;
    }
}

// ---- Group 3 (Speed): duplicate, keyboard shortcuts, recent files.

// Names of the items in an array, for UniqueCopyName.
inline std::vector<std::string>
ItemNames(const nlohmann::json &arr) {
    std::vector<std::string> names;
    if (arr.is_array())
        for (const auto &it : arr)
            names.push_back(it.value("name", ""));
    return names;
}

// Duplicate the selected item in the active tab. Instructions and
// ALU operations get the first free opcode/code (reuse
// FirstFreeCode) and the instruction's encoding is synced to the new
// opcode. Returns false when nothing can be duplicated (empty list,
// full code range, or a tab with no duplicable list).
inline bool DuplicateSelectedItem(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    if (st.active_tab == "Instructions") {
        if (!doc.contains("instruction_set") ||
            !doc["instruction_set"].contains("instructions") ||
            !doc["instruction_set"]["instructions"].is_array())
            return false;
        nlohmann::json &insts = doc["instruction_set"]["instructions"];
        if (insts.empty() || st.sel_instruction < 0 ||
            st.sel_instruction >= static_cast<int>(insts.size()))
            return false;
        const nlohmann::json &src = insts[st.sel_instruction];
        const int code = FirstFreeCode(insts, "opcode", 65535);
        if (code < 0) {
            st.action_msg = "No free instruction opcode left (0-65535 "
                            "are all used).";
            return false;
        }
        nlohmann::json copy = DuplicateItem(
            src,
            UniqueCopyName(ItemNames(insts), src.value("name", "?")));
        copy["opcode"] = code;
        SyncEncodingWithOpcode(copy, code);
        insts.push_back(copy);
        st.sel_instruction = static_cast<int>(insts.size()) - 1;
        st.ClearTextBufs();
        st.MarkDirty();
        PushUndoSnapshot(st);
        return true;
    }
    if (st.active_tab == "Registers") {
        const char *group =
            st.sel_group == 0 ? "general_purpose" : "special";
        if (!doc.contains("registers") ||
            !doc["registers"].contains(group))
            return false;
        nlohmann::json &arr = doc["registers"][group];
        if (!arr.is_array() || arr.empty() || st.sel_register < 0 ||
            st.sel_register >= static_cast<int>(arr.size()))
            return false;
        const nlohmann::json &src = arr[st.sel_register];
        nlohmann::json copy = DuplicateItem(
            src, UniqueCopyName(ItemNames(arr), src.value("name", "?")));
        arr.push_back(copy);
        st.sel_register = static_cast<int>(arr.size()) - 1;
        st.ClearTextBufs();
        st.MarkDirty();
        PushUndoSnapshot(st);
        return true;
    }
    if (st.active_tab == "ALU") {
        if (!doc.contains("alu") || !doc["alu"].contains("operations") ||
            !doc["alu"]["operations"].is_array())
            return false;
        nlohmann::json &ops = doc["alu"]["operations"];
        if (ops.empty() || st.sel_alu_op < 0 ||
            st.sel_alu_op >= static_cast<int>(ops.size()))
            return false;
        const nlohmann::json &src = ops[st.sel_alu_op];
        const int code = FirstFreeCode(ops, "code", 255);
        if (code < 0) {
            st.action_msg =
                "No free ALU code left (0-255 are all used).";
            return false;
        }
        nlohmann::json copy = DuplicateItem(
            src, UniqueCopyName(ItemNames(ops), src.value("name", "?")));
        copy["code"] = code;
        ops.push_back(copy);
        st.sel_alu_op = static_cast<int>(ops.size()) - 1;
        st.ClearTextBufs();
        st.MarkDirty();
        PushUndoSnapshot(st);
        return true;
    }
    if (st.active_tab == "Peripherals") {
        if (!doc.contains("peripherals") ||
            !doc["peripherals"].is_array())
            return false;
        nlohmann::json &periphs = doc["peripherals"];
        if (periphs.empty() || st.sel_peripheral < 0 ||
            st.sel_peripheral >= static_cast<int>(periphs.size()))
            return false;
        const nlohmann::json &src = periphs[st.sel_peripheral];
        nlohmann::json copy = DuplicateItem(
            src,
            UniqueCopyName(ItemNames(periphs), src.value("name", "?")));
        periphs.push_back(copy);
        st.sel_peripheral = static_cast<int>(periphs.size()) - 1;
        st.ClearTextBufs();
        st.MarkDirty();
        PushUndoSnapshot(st);
        return true;
    }
    return false;
}

// Keyboard shortcuts (also Group 1's Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y
// for undo/redo). Never fires while a text field is being edited
// (io.WantTextInput), so typing in a field cannot save, undo or
// duplicate by accident. Redo is checked before undo so
// Ctrl+Shift+Z cannot also match plain Ctrl+Z.
inline void HandleKeyboardShortcuts(ConfiguratorState &st) {
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput)
        return;
    if (ImGui::Shortcut(ImGuiKey_S | ImGuiMod_Ctrl)) {
        if (st.path.empty())
            st.action_msg = "No file path set. Type a path above first.";
        else
            st.SaveToFile(st.path);
    }
    if (ImGui::Shortcut(ImGuiKey_Z | ImGuiMod_Ctrl | ImGuiMod_Shift) ||
        ImGui::Shortcut(ImGuiKey_Y | ImGuiMod_Ctrl)) {
        DoRedo(st);
    } else if (ImGui::Shortcut(ImGuiKey_Z | ImGuiMod_Ctrl)) {
        DoUndo(st);
    }
    if (ImGui::Shortcut(ImGuiKey_D | ImGuiMod_Ctrl))
        DuplicateSelectedItem(st);
}

// Recent files (persisted in the settings file, see
// configurator_main.cpp): one clickable button per path, on one row
// after a "Recent:" label. Loading through RequestLoad keeps the
// unsaved-changes warning.
inline void RenderRecentFiles(ConfiguratorState &st) {
    if (st.settings.recent_files.empty())
        return;
    ImGui::Text("Recent:");
    for (const std::string &recent : st.settings.recent_files) {
        ImGui::SameLine();
        if (ImGui::Button(recent.c_str()))
            RequestLoad(st, recent);
    }
    ImGui::NewLine();
}

// ---- Group 4 (Clarity): help markers and example inserts.

// One uniform tooltip pattern: a grey "(?)" after a field; hovering
// shows the one-sentence explanation.
inline void HelpMarker(const char *text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

// Parse a word value ("0x3000", "123", 123) to an integer; -1 when
// it is not a number.
inline int ExampleWordValue(const nlohmann::json &v) {
    try {
        if (v.is_number_integer())
            return v.get<int>();
        if (v.is_number_unsigned())
            return static_cast<int>(v.get<unsigned>());
        if (v.is_string()) {
            const std::string s = Trim(v.get<std::string>());
            if (s.empty())
                return -1;
            size_t used = 0;
            const long n = std::stol(s, &used, 0);
            if (used != s.size())
                return -1;
            return static_cast<int>(n);
        }
    } catch (...) {
    }
    return -1;
}

// Insert the example timer peripheral (see example_items.hpp).
// The builder always uses 0x3000-0x3003, so the range is shifted
// forward by 4 until it no longer overlaps an existing peripheral:
// inserting a second example stays valid instead of flagging an
// overlap in Problems.
inline void InsertExamplePeripheral(ConfiguratorState &st) {
    nlohmann::json &doc = st.doc;
    if (!doc.contains("peripherals") || !doc["peripherals"].is_array())
        doc["peripherals"] = nlohmann::json::array();
    nlohmann::json &periphs = doc["peripherals"];
    const std::string name =
        UniqueCopyName(ItemNames(periphs), "ExampleTimer");
    nlohmann::json example = ExampleItems::ExamplePeripheral(name);
    int start = ExampleWordValue(example["address_start"]);
    int end = ExampleWordValue(example["address_end"]);
    if (start >= 0 && end >= start) {
        bool overlap = true;
        while (overlap) {
            overlap = false;
            for (const auto &p : periphs) {
                const int os =
                    p.contains("address_start")
                        ? ExampleWordValue(p["address_start"])
                        : -1;
                const int oe =
                    p.contains("address_end")
                        ? ExampleWordValue(p["address_end"])
                        : -1;
                if (os < 0 || oe < os)
                    continue;
                if (start <= oe && os <= end) {
                    overlap = true;
                    break;
                }
            }
            if (overlap) {
                start += 4;
                end += 4;
            }
        }
        if (start != ExampleWordValue(example["address_start"])) {
            std::ostringstream ss;
            ss << "0x" << std::hex << start;
            example["address_start"] = ss.str();
            std::ostringstream es;
            es << "0x" << std::hex << end;
            example["address_end"] = es.str();
        }
    }
    periphs.push_back(example);
    st.sel_peripheral = static_cast<int>(periphs.size()) - 1;
    st.ClearTextBufs();
    st.MarkDirty();
    PushUndoSnapshot(st);
}

// Insert the MathCoProcessor-style sample hook (see
// example_items.hpp): fills the hook when it is absent or empty,
// appends the sample nodes when it already has nodes.
inline void InsertExampleHook(ConfiguratorState &st, nlohmann::json &obj,
                              const char *key) {
    const nlohmann::json example = ExampleItems::ExampleHook();
    if (!obj.contains(key) || !obj[key].is_array() ||
        obj[key].empty())
        obj[key] = example;
    else
        for (const auto &node : example)
            obj[key].push_back(node);
    st.ClearTextBufs();
    st.MarkDirty();
    PushUndoSnapshot(st);
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
        RequestLoad(st, st.path);
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
    ImGui::BeginDisabled(!st.history.canUndo());
    if (ImGui::Button("Undo (Ctrl+Z)")) {
        DoUndo(st);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!st.history.canRedo());
    if (ImGui::Button("Redo (Ctrl+Y)")) {
        DoRedo(st);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        RequestNewTemplate(st);
    }

    RenderRecentFiles(st);

    ImGui::Text("Presets:");
    ImGui::SameLine();
    static const char *presets[] = {"tiny4.json", "8bit.json",
                                    "8bit_harvard.json", "16bit.json",
                                    "16bit_lab.json"};
    for (const char *preset : presets) {
        if (ImGui::Button(preset)) {
            RequestLoad(st, ResolvePresetPath(preset));
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

    ImGui::SameLine();
    static const char *scales[] = {"0.9x", "1.0x", "1.25x", "1.5x"};
    static const double scale_values[] = {0.9, 1.0, 1.25, 1.5};
    int scale_idx = 1;
    for (int i = 0; i < 4; ++i)
        if (std::abs(st.settings.ui_scale - scale_values[i]) < 0.001)
            scale_idx = i;
    if (ImGui::Combo("UI scale", &scale_idx, scales,
                     IM_ARRAYSIZE(scales))) {
        st.settings.ui_scale = scale_values[scale_idx];
        ImGui::GetIO().FontGlobalScale =
            static_cast<float>(st.settings.ui_scale);
        if (!st.settings_path.empty())
            SaveSettings(st.settings_path, st.settings);
    }
}

} // namespace ConfiguratorDetail

inline void UI_Configurator(ConfiguratorState &st) {
    if (st.last_validated_version != st.doc_version)
        st.RefreshValidation();
    ImGui::Begin("CPU Configurator");
    ConfiguratorDetail::HandleKeyboardShortcuts(st);
    ConfiguratorDetail::DrawTopBar(st);
    ImGui::Separator();
    if (ImGui::BeginTabBar("ConfiguratorTabs")) {
        // A clicked problem selects its tab; the flag is consumed
        // (cleared) once that tab is open. The click takes effect on
        // the next frame, since the target tab's BeginTabItem already
        // ran this frame -- a one-frame delay, imperceptible.
        const std::string want_tab = st.goto_tab;
        const auto tab_flags = [&want_tab](const char *label) {
            return want_tab == label ? ImGuiTabItemFlags_SetSelected
                                     : ImGuiTabItemFlags_None;
        };
        if (ImGui::BeginTabItem("System", nullptr, tab_flags("System"))) {
            if (want_tab == "System")
                st.goto_tab.clear();
            st.active_tab = "System";
            ConfiguratorDetail::DrawSystemTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Registers", nullptr,
                                tab_flags("Registers"))) {
            if (want_tab == "Registers")
                st.goto_tab.clear();
            st.active_tab = "Registers";
            ConfiguratorDetail::DrawRegistersTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory", nullptr, tab_flags("Memory"))) {
            if (want_tab == "Memory")
                st.goto_tab.clear();
            st.active_tab = "Memory";
            ConfiguratorDetail::DrawMemoryTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ALU", nullptr, tab_flags("ALU"))) {
            if (want_tab == "ALU")
                st.goto_tab.clear();
            st.active_tab = "ALU";
            ConfiguratorDetail::DrawAluTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Instructions", nullptr,
                                tab_flags("Instructions"))) {
            if (want_tab == "Instructions")
                st.goto_tab.clear();
            st.active_tab = "Instructions";
            ConfiguratorDetail::DrawInstructionsTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Problems", nullptr,
                                tab_flags("Problems"))) {
            if (want_tab == "Problems")
                st.goto_tab.clear();
            st.active_tab = "Problems";
            ConfiguratorDetail::DrawProblemsTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Peripherals", nullptr,
                                tab_flags("Peripherals"))) {
            if (want_tab == "Peripherals")
                st.goto_tab.clear();
            st.active_tab = "Peripherals";
            ConfiguratorDetail::DrawPeripheralsTab(st);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("JSON", nullptr, tab_flags("JSON"))) {
            if (want_tab == "JSON")
                st.goto_tab.clear();
            st.active_tab = "JSON";
            ConfiguratorDetail::DrawJsonTab(st);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ConfiguratorDetail::RenderDeleteModal(st);
    ConfiguratorDetail::RenderOpenConfirmModal(st);
    ConfiguratorDetail::RenderQuitModal(st);
    ImGui::End();
}
