// Pure (no ImGui) logic for the configurator's persistent settings:
// the UI scale and the list of recently opened files.
//
// Settings file contract (fixed -- other groups depend on it, do not
// change it): one file, plain lines, `scale=<float>` (e.g. scale=1.25)
// and `recent=<path>` lines, most-recent first, at most 8 recent
// entries. Unknown lines are ignored on load. Save writes scale first,
// then the recent lines. The caller passes the full path in (the UI
// uses ~/.config/cpu_configurator/settings.txt); these functions never
// touch the real home directory themselves, so tests can point them at
// a temp directory.
//
// What lives here (testable without a GUI):
//   - AppSettings: ui_scale (default 1.0) + recent_files.
//   - LoadSettings(path): defaults when the file is missing or
//     unreadable; unknown lines, blank lines and unparsable scale
//     values are ignored.
//   - SaveSettings(path, s): creates parent directories if needed.
//   - AddRecent(s, path): moves the path to the front, dedupes, caps
//     at 8.
//
// Names are at global scope exactly as the Group 3 brief specifies,
// so the UI code in ui_configurator.hpp can call them unqualified.

#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace AppSettingsDetail {

// Trim leading/trailing ASCII whitespace (and a trailing '\r' from
// CRLF files) from one line.
inline std::string TrimLine(const std::string &s) {
    size_t begin = 0;
    while (begin < s.size() &&
           (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r' ||
            s[begin] == '\n'))
        ++begin;
    size_t end = s.size();
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                           s[end - 1] == '\r' || s[end - 1] == '\n'))
        --end;
    return s.substr(begin, end - begin);
}

inline constexpr size_t kMaxRecentFiles = 8;

} // namespace AppSettingsDetail

struct AppSettings {
    double ui_scale = 1.0;
    std::vector<std::string> recent_files; // most-recent first, max 8
};

// Move `path` to the front of the recent list, dropping any older
// entry for the same path, and cap the list at 8 entries. Empty
// paths are ignored.
inline void AddRecent(AppSettings &s, const std::string &path) {
    if (path.empty())
        return;
    s.recent_files.erase(
        std::remove(s.recent_files.begin(), s.recent_files.end(), path),
        s.recent_files.end());
    s.recent_files.insert(s.recent_files.begin(), path);
    if (s.recent_files.size() > AppSettingsDetail::kMaxRecentFiles)
        s.recent_files.resize(AppSettingsDetail::kMaxRecentFiles);
}

// Read settings from `path`. A missing or unreadable file returns
// defaults (scale 1.0, no recents). Unknown lines are ignored; a
// `scale=` line that does not parse, or is not finite and > 0, is
// ignored too; `recent=` lines are deduped (first occurrence wins)
// and capped at 8, in file order (most-recent first).
inline AppSettings LoadSettings(const std::string &path) {
    AppSettings s;
    std::ifstream f(path);
    if (!f.is_open())
        return s;
    std::string line;
    while (std::getline(f, line)) {
        const std::string t = AppSettingsDetail::TrimLine(line);
        if (t.empty())
            continue;
        if (t.rfind("scale=", 0) == 0) {
            try {
                const double v =
                    std::stod(AppSettingsDetail::TrimLine(t.substr(6)));
                if (std::isfinite(v) && v > 0.0)
                    s.ui_scale = v;
            } catch (...) {
                // Unparsable scale: keep the current value.
            }
        } else if (t.rfind("recent=", 0) == 0) {
            const std::string p =
                AppSettingsDetail::TrimLine(t.substr(7));
            if (!p.empty() &&
                s.recent_files.size() <
                    AppSettingsDetail::kMaxRecentFiles &&
                std::find(s.recent_files.begin(), s.recent_files.end(),
                          p) == s.recent_files.end())
                s.recent_files.push_back(p);
        }
        // Anything else: unknown line, ignored.
    }
    return s;
}

// Write settings to `path`: `scale=<value>` first, then one
// `recent=<path>` line per recent file, most-recent first. Parent
// directories are created when needed. Returns false when the file
// cannot be written.
inline bool SaveSettings(const std::string &path, const AppSettings &s) {
    if (path.empty())
        return false;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec)
            return false;
    }
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open())
        return false;
    std::ostringstream scale;
    scale << std::setprecision(15) << std::defaultfloat << s.ui_scale;
    f << "scale=" << scale.str() << '\n';
    for (const std::string &recent : s.recent_files)
        f << "recent=" << recent << '\n';
    f.flush();
    return f.good();
}
