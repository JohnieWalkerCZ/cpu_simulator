#pragma once
// Shared two-way launch helper between cpu_sim and cpu_configurator.
//
// Both apps are built into the same directory, so a sibling executable is
// found next to the running one (via /proc/self/exe on Linux, falling back
// to the argv[0] directory and then the working directory). The launched
// app is started detached in the background; the caller keeps running.
// A missing sibling is reported as a message instead of failing silently.

#include <cstdlib>
#include <filesystem>
#include <string>

namespace AppLauncher {

// Quote one shell argument with single quotes (handles embedded quotes).
inline std::string ShellQuote(const std::string &value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

inline std::filesystem::path SiblingDir(const char *argv0) {
    std::error_code ec;
    std::filesystem::path exe =
        std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !exe.empty() && exe.has_parent_path())
        return exe.parent_path();
    if (argv0 != nullptr && *argv0 != '\0') {
        std::filesystem::path p(argv0);
        if (p.has_parent_path() && !p.parent_path().empty())
            return p.parent_path();
    }
    return std::filesystem::current_path();
}

inline bool FindSiblingExe(const char *argv0, const std::string &exe_name,
                           std::filesystem::path &out) {
    std::filesystem::path candidate = SiblingDir(argv0) / exe_name;
    if (std::filesystem::exists(candidate)) {
        out = candidate;
        return true;
    }
    candidate = std::filesystem::current_path() / exe_name;
    if (std::filesystem::exists(candidate)) {
        out = candidate;
        return true;
    }
    return false;
}

// Launch `<sibling-dir>/<exe_name> "<config_path>"` detached.
// Returns false (with `message` explaining why) when the sibling binary
// cannot be found or the shell fails to start it.
inline bool LaunchSiblingApp(const char *argv0, const std::string &exe_name,
                             const std::string &config_path,
                             std::string &message) {
    std::filesystem::path exe;
    if (!FindSiblingExe(argv0, exe_name, exe)) {
        message = "Cannot find " + exe_name +
                  " next to this executable. Build it first: cmake --build "
                  "build --target " +
                  exe_name + ".";
        return false;
    }
    std::error_code ec;
    std::filesystem::path abs_config =
        std::filesystem::absolute(config_path, ec);
    if (ec)
        abs_config = config_path;
    const std::string command = ShellQuote(exe.string()) + " " +
                                ShellQuote(abs_config.string()) +
                                " >/dev/null 2>&1 &";
    const int rc = std::system(command.c_str());
    if (rc != 0) {
        message = "Failed to launch " + exe_name + " (shell returned " +
                  std::to_string(rc) + ").";
        return false;
    }
    message = "Launched " + exe_name + " with " + abs_config.string() + ".";
    return true;
}

} // namespace AppLauncher
