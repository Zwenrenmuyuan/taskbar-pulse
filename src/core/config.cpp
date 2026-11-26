#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include "core/config.hpp"
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <vector>

namespace {
constexpr wchar_t kSection[] = L"General";
constexpr wchar_t kOffsetsSection[] = L"MonitorOffsets";
constexpr wchar_t kRunRegPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"Win11TaskbarMonitor";
constexpr wchar_t kThemeRegPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kConfigSubPath[] = L"\\.config\\taskbar-pulse\\config.ini";

std::wstring GetUserProfileDir() {
    DWORD needed = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (needed > 1) {
        std::wstring buf(needed, L'\0');
        DWORD got = GetEnvironmentVariableW(L"USERPROFILE", buf.data(), needed);
        if (got > 0 && got < needed) {
            buf.resize(got);
            while (!buf.empty() && (buf.back() == L'\\' || buf.back() == L'/')) {
                buf.pop_back();
            }
            if (!buf.empty()) return buf;
        }
    }

    PWSTR knownPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &knownPath)) && knownPath) {
        std::wstring dir(knownPath);
        CoTaskMemFree(knownPath);
        while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
            dir.pop_back();
        }
        if (!dir.empty()) return dir;
    }
    return {};
}

void EnsureParentDirectory(const std::wstring& filePath) {
    auto pos = filePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos || pos == 0) return;
    std::wstring dir = filePath.substr(0, pos);
    for (wchar_t& ch : dir) {
        if (ch == L'/') ch = L'\\';
    }
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
}
} // namespace

std::wstring SanitizeMonitorKey(const std::wstring& deviceName) {
    std::wstring s = deviceName;
    if (s.rfind(L"\\\\.\\", 0) == 0) {
        s.erase(0, 4);
    }
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t ch : s) {
        if (ch == L'=' || ch == L'[' || ch == L']' || ch == L'\r' || ch == L'\n') continue;
        out.push_back(static_cast<wchar_t>(std::towupper(ch)));
    }
    return out;
}

int GetMonitorOffsetDip(const AppConfig& cfg, bool isPrimary, const std::wstring& deviceName) {
    if (isPrimary) return cfg.offsetXDip;
    std::wstring key = SanitizeMonitorKey(deviceName);
    if (key.empty()) return 0;
    auto it = cfg.secondaryOffsetsDip.find(key);
    return (it != cfg.secondaryOffsetsDip.end()) ? it->second : 0;
}

void SetMonitorOffsetDip(AppConfig& cfg, bool isPrimary, const std::wstring& deviceName, int offsetDip) {
    if (isPrimary) {
        cfg.offsetXDip = offsetDip;
        return;
    }
    std::wstring key = SanitizeMonitorKey(deviceName);
    if (key.empty()) return;
    if (offsetDip == 0) {
        cfg.secondaryOffsetsDip.erase(key);
    } else {
        cfg.secondaryOffsetsDip[key] = offsetDip;
    }
}

void ResetAllMonitorOffsets(AppConfig& cfg) {
    cfg.offsetXDip = 0;
    cfg.secondaryOffsetsDip.clear();
}

std::wstring GetExePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return {};
        if (len < buf.size() - 1) {
            buf.resize(len);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

std::wstring GetConfigIniPath() {
    std::wstring profile = GetUserProfileDir();
    if (!profile.empty()) {
        return profile + kConfigSubPath;
    }
    std::wstring exe = GetExePath();
    auto pos = exe.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"config.ini";
    return exe.substr(0, pos + 1) + L"config.ini";
}

AppConfig LoadConfigFromPath(const std::wstring& path) {
    const AppConfig defaults{};
    AppConfig cfg = defaults;

    int style = static_cast<int>(GetPrivateProfileIntW(
        kSection, L"VisualStyle", static_cast<INT>(defaults.style), path.c_str()));
    if (style == static_cast<int>(VisualStyle::Capsule)) cfg.style = VisualStyle::Capsule;
    else if (style == static_cast<int>(VisualStyle::Minimal)) cfg.style = VisualStyle::Minimal;
    else cfg.style = defaults.style;

    int theme = static_cast<int>(GetPrivateProfileIntW(
        kSection, L"ThemeMode", static_cast<INT>(defaults.theme), path.c_str()));
    if (theme == static_cast<int>(ThemeMode::Light)) cfg.theme = ThemeMode::Light;
    else if (theme == static_cast<int>(ThemeMode::Dark)) cfg.theme = ThemeMode::Dark;
    else if (theme == static_cast<int>(ThemeMode::Auto)) cfg.theme = ThemeMode::Auto;
    else cfg.theme = defaults.theme;

    cfg.showMiniBars = GetPrivateProfileIntW(
        kSection, L"ShowMiniBars", defaults.showMiniBars ? 1 : 0, path.c_str()) != 0;

    int r2c1 = static_cast<int>(GetPrivateProfileIntW(
        kSection, L"Row2Col1Mode", static_cast<INT>(defaults.row2Col1), path.c_str()));
    if (r2c1 == static_cast<int>(Row2Col1Mode::CpuFreq)) cfg.row2Col1 = Row2Col1Mode::CpuFreq;
    else if (r2c1 == static_cast<int>(Row2Col1Mode::MemPct)) cfg.row2Col1 = Row2Col1Mode::MemPct;
    else if (r2c1 == static_cast<int>(Row2Col1Mode::Hide)) cfg.row2Col1 = Row2Col1Mode::Hide;
    else if (r2c1 == static_cast<int>(Row2Col1Mode::CpuTemp)) cfg.row2Col1 = Row2Col1Mode::CpuTemp;
    else cfg.row2Col1 = defaults.row2Col1;

    cfg.showGpuTemp = GetPrivateProfileIntW(
        kSection, L"ShowGpuTemp", defaults.showGpuTemp ? 1 : 0, path.c_str()) != 0;
    cfg.showGoldPrice = GetPrivateProfileIntW(
        kSection, L"ShowGoldPrice", defaults.showGoldPrice ? 1 : 0, path.c_str()) != 0;

    int refresh = static_cast<int>(GetPrivateProfileIntW(
        kSection, L"RefreshMs", defaults.refreshMs, path.c_str()));
    cfg.refreshMs = std::clamp(refresh, 250, 10000);

    wchar_t offsetBuf[32]{};
    GetPrivateProfileStringW(kSection, L"OffsetXDip", L"0", offsetBuf, _countof(offsetBuf), path.c_str());
    cfg.offsetXDip = _wtoi(offsetBuf);

    int dispMode = static_cast<int>(GetPrivateProfileIntW(
        kSection, L"DisplayMode", static_cast<INT>(defaults.displayMode), path.c_str()));
    if (dispMode == static_cast<int>(DisplayTargetMode::All)) cfg.displayMode = DisplayTargetMode::All;
    else if (dispMode == static_cast<int>(DisplayTargetMode::Primary)) cfg.displayMode = DisplayTargetMode::Primary;
    else if (dispMode == static_cast<int>(DisplayTargetMode::Specific)) cfg.displayMode = DisplayTargetMode::Specific;
    else cfg.displayMode = defaults.displayMode;

    wchar_t devBuf[128]{};
    GetPrivateProfileStringW(kSection, L"DisplayDevice", L"", devBuf, _countof(devBuf), path.c_str());
    cfg.displayDevice = devBuf;
    if (cfg.displayMode == DisplayTargetMode::Specific && cfg.displayDevice.empty()) {
        cfg.displayMode = DisplayTargetMode::Primary;
    }

    std::vector<wchar_t> secBuf(4096, L'\0');
    DWORD secLen = GetPrivateProfileSectionW(kOffsetsSection, secBuf.data(), static_cast<DWORD>(secBuf.size()), path.c_str());
    if (secLen > 0) {
        const wchar_t* cur = secBuf.data();
        while (*cur != L'\0') {
            std::wstring entry(cur);
            cur += entry.size() + 1;
            auto eq = entry.find(L'=');
            if (eq == std::wstring::npos || eq == 0) continue;
            std::wstring key = SanitizeMonitorKey(entry.substr(0, eq));
            int val = _wtoi(entry.substr(eq + 1).c_str());
            if (!key.empty() && val != 0) {
                cfg.secondaryOffsetsDip[key] = val;
            }
        }
    }

    return cfg;
}

void SaveConfigToPath(const AppConfig& cfg, const std::wstring& path) {
    EnsureParentDirectory(path);
    auto writeInt = [&](const wchar_t* key, int val) {
        wchar_t buf[32];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", val);
        WritePrivateProfileStringW(kSection, key, buf, path.c_str());
    };
    writeInt(L"VisualStyle", static_cast<int>(cfg.style));
    writeInt(L"ThemeMode", static_cast<int>(cfg.theme));
    writeInt(L"ShowMiniBars", cfg.showMiniBars ? 1 : 0);
    writeInt(L"Row2Col1Mode", static_cast<int>(cfg.row2Col1));
    writeInt(L"ShowGpuTemp", cfg.showGpuTemp ? 1 : 0);
    writeInt(L"ShowGoldPrice", cfg.showGoldPrice ? 1 : 0);
    writeInt(L"RefreshMs", cfg.refreshMs);
    writeInt(L"OffsetXDip", cfg.offsetXDip);
    writeInt(L"DisplayMode", static_cast<int>(cfg.displayMode));
    WritePrivateProfileStringW(kSection, L"DisplayDevice",
        cfg.displayMode == DisplayTargetMode::Specific ? cfg.displayDevice.c_str() : L"",
        path.c_str());

    WritePrivateProfileStringW(kOffsetsSection, nullptr, nullptr, path.c_str());
    for (const auto& kv : cfg.secondaryOffsetsDip) {
        if (kv.first.empty() || kv.second == 0) continue;
        wchar_t buf[32];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", kv.second);
        WritePrivateProfileStringW(kOffsetsSection, kv.first.c_str(), buf, path.c_str());
    }
}

AppConfig LoadConfig() {
    return LoadConfigFromPath(GetConfigIniPath());
}

void SaveConfig(const AppConfig& cfg) {
    SaveConfigToPath(cfg, GetConfigIniPath());
}

namespace {
bool g_themeCacheValid = false;
bool g_cachedSystemLight = false;
} // namespace

void InvalidateSystemThemeCache() {
    g_themeCacheValid = false;
}

bool IsSystemLightTheme() {
    if (g_themeCacheValid) return g_cachedSystemLight;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kThemeRegPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        g_cachedSystemLight = false;
        g_themeCacheValid = true;
        return false;
    }
    DWORD val = 0;
    DWORD sz = sizeof(val);
    DWORD type = 0;
    bool light = false;
    if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", nullptr, &type, reinterpret_cast<LPBYTE>(&val), &sz) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        light = (val != 0);
    }
    RegCloseKey(hKey);
    g_cachedSystemLight = light;
    g_themeCacheValid = true;
    return light;
}

bool ResolveEffectiveLightTheme(const AppConfig& cfg) {
    if (cfg.theme == ThemeMode::Light) return true;
    if (cfg.theme == ThemeMode::Dark) return false;
    return IsSystemLightTheme();
}

bool IsAutoStartEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD sz = 0;
    bool exists = (RegQueryValueExW(hKey, kRunValueName, nullptr, &type, nullptr, &sz) == ERROR_SUCCESS && sz > 0);
    RegCloseKey(hKey);
    return exists;
}

bool SetAutoStartEnabled(bool enabled) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    bool ok = false;
    if (enabled) {
        std::wstring cmd = L"\"" + GetExePath() + L"\"";
        DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
        ok = (RegSetValueExW(hKey, kRunValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), bytes) == ERROR_SUCCESS);
    } else {
        LONG r = RegDeleteValueW(hKey, kRunValueName);
        ok = (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hKey);
    return ok;
}
