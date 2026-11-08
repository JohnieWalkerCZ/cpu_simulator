// Tests for the persistent configurator settings
// (src/ui/app_settings.hpp): load/save round-trip, missing-file
// defaults, recent-list dedupe/ordering/cap, unknown lines ignored,
// and scale parsing. All files live under a temp directory created
// for this run -- the real home directory is never touched.
#include "ui/app_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static fs::path TestDir() {
    return fs::temp_directory_path() / "cpu_configurator_settings_test";
}

static std::string ReadFile(const fs::path &p) {
    std::ifstream f(p);
    assert(f.is_open());
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static void WriteFile(const fs::path &p, const std::string &text) {
    std::ofstream f(p, std::ios::trunc);
    assert(f.is_open());
    f << text;
    assert(f.good());
}

int main() {
    const fs::path dir = TestDir();
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string settings_path = (dir / "settings.txt").string();

    // Missing file: defaults, no crash.
    {
        AppSettings s = LoadSettings(settings_path);
        assert(s.ui_scale == 1.0);
        assert(s.recent_files.empty());
    }

    // Round-trip: scale + recents survive save/load, scale first.
    {
        AppSettings s;
        s.ui_scale = 1.25;
        AddRecent(s, "/tmp/a.json");
        AddRecent(s, "/tmp/b.json");
        AddRecent(s, "/tmp/c.json");
        assert(SaveSettings(settings_path, s));
        assert(ReadFile(dir / "settings.txt") ==
               "scale=1.25\nrecent=/tmp/c.json\nrecent=/tmp/b.json\n"
               "recent=/tmp/a.json\n");
        AppSettings back = LoadSettings(settings_path);
        assert(back.ui_scale == 1.25);
        assert(back.recent_files ==
               (std::vector<std::string>{"/tmp/c.json", "/tmp/b.json",
                                         "/tmp/a.json"}));
    }

    // AddRecent: new path to the front, existing path moved (not
    // duplicated), empty path ignored.
    {
        AppSettings s;
        AddRecent(s, "/tmp/a.json");
        AddRecent(s, "/tmp/b.json");
        AddRecent(s, "/tmp/a.json");
        assert(s.recent_files ==
               (std::vector<std::string>{"/tmp/a.json", "/tmp/b.json"}));
        AddRecent(s, "");
        assert(s.recent_files.size() == 2);
    }

    // AddRecent caps at 8: adding 10 keeps the 8 most recent, in
    // order, and the two oldest are dropped.
    {
        AppSettings s;
        for (int i = 0; i < 10; ++i)
            AddRecent(s, "/tmp/f" + std::to_string(i) + ".json");
        assert(s.recent_files.size() == 8);
        assert(s.recent_files.front() == "/tmp/f9.json");
        assert(s.recent_files.back() == "/tmp/f2.json");
        // Round-trip through the file keeps the cap.
        assert(SaveSettings(settings_path, s));
        AppSettings back = LoadSettings(settings_path);
        assert(back.recent_files.size() == 8);
        assert(back.recent_files == s.recent_files);
    }

    // Load: unknown lines, blank lines and CRLF are ignored; a
    // duplicate recent line keeps the first occurrence only; a file
    // with more than 8 recents is capped at 8.
    {
        WriteFile(dir / "settings.txt",
                  "scale=1.5\r\n"
                  "\n"
                  "theme=dark\n"
                  "recent=/tmp/a.json\n"
                  "recent=/tmp/b.json\n"
                  "recent=/tmp/a.json\n"
                  "recent=/tmp/c.json\n"
                  "recent=/tmp/d.json\n"
                  "recent=/tmp/e.json\n"
                  "recent=/tmp/f.json\n"
                  "recent=/tmp/g.json\n"
                  "recent=/tmp/h.json\n"
                  "recent=/tmp/i.json\n"
                  "recent=/tmp/j.json\n"
                  "some random line\n");
        AppSettings s = LoadSettings(settings_path);
        assert(s.ui_scale == 1.5);
        assert(s.recent_files.size() == 8);
        assert(s.recent_files ==
               (std::vector<std::string>{
                   "/tmp/a.json", "/tmp/b.json", "/tmp/c.json",
                   "/tmp/d.json", "/tmp/e.json", "/tmp/f.json",
                   "/tmp/g.json", "/tmp/h.json"}));
    }

    // Load: bad scale values are ignored (value stays at the default
    // when nothing valid precedes them, and at the last good value
    // when one does); scale=0 / negative / NaN are not valid scales.
    {
        WriteFile(dir / "settings.txt", "scale=abc\nscale=-2\n");
        assert(LoadSettings(settings_path).ui_scale == 1.0);
        WriteFile(dir / "settings.txt", "scale=0\n");
        assert(LoadSettings(settings_path).ui_scale == 1.0);
        WriteFile(dir / "settings.txt", "scale=nan\n");
        assert(LoadSettings(settings_path).ui_scale == 1.0);
        WriteFile(dir / "settings.txt", "scale=2\nscale=bogus\n");
        assert(LoadSettings(settings_path).ui_scale == 2.0);
        WriteFile(dir / "settings.txt", "scale= 1.75 \n");
        assert(LoadSettings(settings_path).ui_scale == 1.75);
        // "recent=" with an empty path is ignored.
        WriteFile(dir / "settings.txt", "recent=\nrecent=   \n");
        assert(LoadSettings(settings_path).recent_files.empty());
    }

    // Save creates parent directories that do not exist yet.
    {
        const std::string nested =
            (dir / "deep" / "deeper" / "settings.txt").string();
        AppSettings s;
        s.ui_scale = 0.9;
        AddRecent(s, "/tmp/x.json");
        assert(SaveSettings(nested, s));
        assert(fs::exists(dir / "deep" / "deeper" / "settings.txt"));
        AppSettings back = LoadSettings(nested);
        assert(back.ui_scale == 0.9);
        assert(back.recent_files ==
               (std::vector<std::string>{"/tmp/x.json"}));
    }

    // Save with an empty path fails instead of crashing.
    {
        AppSettings s;
        assert(!SaveSettings("", s));
    }

    fs::remove_all(dir);
    std::cout << "All app settings tests passed!" << std::endl;
    return 0;
}
