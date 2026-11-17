#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/config.hpp"
#include <algorithm>
#include <cstdio>

namespace {
constexpr wchar_t kSection[] = L"General";
constexpr wchar_t kRunRegPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"Win11TaskbarMonitor";
constexpr wchar_t kThemeRegPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
} // namespace

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
    std::wstring exe = GetExePath();
    auto pos = exe.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"TaskbarMonitor.ini";
    return exe.substr(0, pos + 1) + L"TaskbarMonitor.ini";
}

AppConfig LoadConfig() {
    AppConfig cfg;
    std::wstring path = GetConfigIniPath();

    int style = static_cast<int>(GetPrivateProfileIntW(kSection, L"VisualStyle", 0, path.c_str()));
    cfg.style = (style == 1) ? VisualStyle::Minimal : VisualStyle::Capsule;

    int theme = static_cast<int>(GetPrivateProfileIntW(kSection, L"ThemeMode", 0, path.c_str()));
    if (theme == 1) cfg.theme = ThemeMode::Light;
    else if (theme == 2) cfg.theme = ThemeMode::Dark;
    else cfg.theme = ThemeMode::Auto;

    cfg.showMiniBars = GetPrivateProfileIntW(kSection, L"ShowMiniBars", 1, path.c_str()) != 0;

    int r2c1 = static_cast<int>(GetPrivateProfileIntW(kSection, L"Row2Col1Mode", 0, path.c_str()));
    if (r2c1 == 1) cfg.row2Col1 = Row2Col1Mode::MemPct;
    else if (r2c1 == 2) cfg.row2Col1 = Row2Col1Mode::Hide;
    else if (r2c1 == 3) cfg.row2Col1 = Row2Col1Mode::CpuTemp;
    else cfg.row2Col1 = Row2Col1Mode::CpuFreq;

    cfg.showGpuTemp = GetPrivateProfileIntW(kSection, L"ShowGpuTemp", 1, path.c_str()) != 0;
    cfg.showGoldPrice = GetPrivateProfileIntW(kSection, L"ShowGoldPrice", 1, path.c_str()) != 0;
    cfg.hideInFullscreen = GetPrivateProfileIntW(kSection, L"HideInFullscreen", 1, path.c_str()) != 0;

    int refresh = static_cast<int>(GetPrivateProfileIntW(kSection, L"RefreshMs", 1000, path.c_str()));
    cfg.refreshMs = std::clamp(refresh, 250, 10000);

    wchar_t offsetBuf[32]{};
    GetPrivateProfileStringW(kSection, L"OffsetXDip", L"0", offsetBuf, 32, path.c_str());
    cfg.offsetXDip = _wtoi(offsetBuf);

    return cfg;
}

void SaveConfig(const AppConfig& cfg) {
    std::wstring path = GetConfigIniPath();
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
    writeInt(L"HideInFullscreen", cfg.hideInFullscreen ? 1 : 0);
    writeInt(L"RefreshMs", cfg.refreshMs);
    writeInt(L"OffsetXDip", cfg.offsetXDip);
}

bool IsSystemLightTheme() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kThemeRegPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
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
