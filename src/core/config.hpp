#pragma once

#include <string>

enum class VisualStyle : int {
    Capsule = 0,
    Minimal = 1,
};

enum class ThemeMode : int {
    Auto = 0,
    Light = 1,
    Dark = 2,
};

enum class Row2Col1Mode : int {
    CpuFreq = 0,
    MemPct = 1,
    Hide = 2,
    CpuTemp = 3,
};

struct AppConfig {
    VisualStyle style = VisualStyle::Capsule;
    ThemeMode theme = ThemeMode::Auto;
    bool showMiniBars = true;
    Row2Col1Mode row2Col1 = Row2Col1Mode::CpuFreq;
    bool showGpuTemp = true;
    bool showGoldPrice = true;
    bool hideInFullscreen = true;
    int refreshMs = 1000;
    int offsetXDip = 0;
};

std::wstring GetExePath();
std::wstring GetConfigIniPath();
AppConfig LoadConfig();
void SaveConfig(const AppConfig& cfg);

bool IsSystemLightTheme();
bool ResolveEffectiveLightTheme(const AppConfig& cfg);

bool IsAutoStartEnabled();
bool SetAutoStartEnabled(bool enabled);
