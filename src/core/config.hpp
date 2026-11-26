#pragma once

#include <map>
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

enum class DisplayTargetMode : int {
    All = 0,
    Primary = 1,
    Specific = 2,
};

struct AppConfig {
    VisualStyle style = VisualStyle::Minimal;
    ThemeMode theme = ThemeMode::Auto;
    bool showMiniBars = true;
    Row2Col1Mode row2Col1 = Row2Col1Mode::CpuTemp;
    bool showGpuTemp = true;
    bool showGoldPrice = true;
    int refreshMs = 1000;
    int offsetXDip = 0;
    DisplayTargetMode displayMode = DisplayTargetMode::Primary;
    std::wstring displayDevice;
    std::map<std::wstring, int> secondaryOffsetsDip;
};

std::wstring SanitizeMonitorKey(const std::wstring& deviceName);
int GetMonitorOffsetDip(const AppConfig& cfg, bool isPrimary, const std::wstring& deviceName);
void SetMonitorOffsetDip(AppConfig& cfg, bool isPrimary, const std::wstring& deviceName, int offsetDip);
void ResetAllMonitorOffsets(AppConfig& cfg);

std::wstring GetExePath();
std::wstring GetConfigIniPath();
AppConfig LoadConfigFromPath(const std::wstring& path);
void SaveConfigToPath(const AppConfig& cfg, const std::wstring& path);
AppConfig LoadConfig();
void SaveConfig(const AppConfig& cfg);

bool IsSystemLightTheme();
void InvalidateSystemThemeCache();
bool ResolveEffectiveLightTheme(const AppConfig& cfg);

bool IsAutoStartEnabled();
bool SetAutoStartEnabled(bool enabled);
