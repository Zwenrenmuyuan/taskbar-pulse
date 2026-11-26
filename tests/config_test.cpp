#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../src/core/config.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

int main() {
    namespace fs = std::filesystem;

    // 1. Verify GetConfigIniPath() resolves to %USERPROFILE%\.config\taskbar-pulse\config.ini
    const fs::path tempRoot = fs::temp_directory_path() /
        (L"taskbar_pulse_cfg_test_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(tempRoot, ec);
    fs::create_directories(tempRoot, ec);

    std::wstring prevProfile;
    bool hadPrevProfile = false;
    DWORD needed = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (needed > 1) {
        prevProfile.resize(needed);
        DWORD got = GetEnvironmentVariableW(L"USERPROFILE", prevProfile.data(), needed);
        if (got > 0 && got < needed) {
            prevProfile.resize(got);
            hadPrevProfile = true;
        }
    }

    assert(SetEnvironmentVariableW(L"USERPROFILE", tempRoot.c_str()));
    std::wstring resolved = GetConfigIniPath();
    std::wstring expected = (tempRoot / L".config" / L"taskbar-pulse" / L"config.ini").wstring();
    assert(resolved == expected);

    // 2. Missing config file returns expected default preferences (with screen-neutral 0 offset)
    AppConfig def = LoadConfigFromPath(resolved);
    assert(def.style == VisualStyle::Minimal);
    assert(def.theme == ThemeMode::Auto);
    assert(def.showMiniBars == true);
    assert(def.row2Col1 == Row2Col1Mode::CpuTemp);
    assert(def.showGpuTemp == true);
    assert(def.showGoldPrice == true);
    assert(def.refreshMs == 1000);
    assert(def.offsetXDip == 0);
    assert(def.displayMode == DisplayTargetMode::Primary);
    assert(def.displayDevice.empty());
    assert(def.secondaryOffsetsDip.empty());

    // 3. SaveConfigToPath automatically creates .config\taskbar-pulse and round-trips all fields
    AppConfig custom{};
    custom.style = VisualStyle::Capsule;
    custom.theme = ThemeMode::Dark;
    custom.showMiniBars = false;
    custom.row2Col1 = Row2Col1Mode::MemPct;
    custom.showGpuTemp = false;
    custom.showGoldPrice = false;
    custom.refreshMs = 500;
    custom.offsetXDip = -240;
    custom.displayMode = DisplayTargetMode::Specific;
    custom.displayDevice = L"\\\\.\\DISPLAY2";
    SetMonitorOffsetDip(custom, false, L"\\\\.\\DISPLAY2", 128);

    SaveConfigToPath(custom, resolved);
    assert(fs::exists(resolved));

    AppConfig loaded = LoadConfigFromPath(resolved);
    assert(loaded.style == VisualStyle::Capsule);
    assert(loaded.theme == ThemeMode::Dark);
    assert(loaded.showMiniBars == false);
    assert(loaded.row2Col1 == Row2Col1Mode::MemPct);
    assert(loaded.showGpuTemp == false);
    assert(loaded.showGoldPrice == false);
    assert(loaded.refreshMs == 500);
    assert(loaded.offsetXDip == -240);
    assert(loaded.displayMode == DisplayTargetMode::Specific);
    assert(loaded.displayDevice == L"\\\\.\\DISPLAY2");
    assert(GetMonitorOffsetDip(loaded, false, L"\\\\.\\DISPLAY2") == 128);

    // Restore USERPROFILE and clean up temp dir
    SetEnvironmentVariableW(L"USERPROFILE", hadPrevProfile ? prevProfile.c_str() : nullptr);
    fs::remove_all(tempRoot, ec);

    std::puts("PASS: config path (.config/taskbar-pulse/config.ini), defaults, auto-mkdir, and round-trip");
    return 0;
}
