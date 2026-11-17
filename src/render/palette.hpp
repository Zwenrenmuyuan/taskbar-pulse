#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>

template <typename T>
inline void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

inline D2D1_COLOR_F Rgba(uint8_t r, uint8_t g, uint8_t b, float a = 1.0f) {
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

struct Palette {
    bool light = true;

    D2D1_COLOR_F capsuleBg{};
    D2D1_COLOR_F capsuleBgHover{};
    D2D1_COLOR_F capsuleBgPressed{};
    D2D1_COLOR_F capsuleBorder{};
    D2D1_COLOR_F capsuleHighlight{};
    D2D1_COLOR_F divider{};
    D2D1_COLOR_F trackBg{};

    D2D1_COLOR_F label{};
    D2D1_COLOR_F labelStrong{};
    D2D1_COLOR_F ok{};
    D2D1_COLOR_F okMem{};
    D2D1_COLOR_F warn{};
    D2D1_COLOR_F hot{};
    D2D1_COLOR_F freq{};
    D2D1_COLOR_F netUp{};
    D2D1_COLOR_F netDown{};
    D2D1_COLOR_F netIdle{};

    // Flyout specific
    D2D1_COLOR_F flyBg{};
    D2D1_COLOR_F flyBorder{};
    D2D1_COLOR_F cardBg{};
    D2D1_COLOR_F cardBorder{};
    D2D1_COLOR_F btnBg{};
    D2D1_COLOR_F btnHover{};
    D2D1_COLOR_F btnText{};
};

inline Palette MakePalette(bool light, bool minimal = false) {
    Palette p;
    p.light = light;
    if (light) {
        p.capsuleBg = Rgba(255, 255, 255, 0.58f);
        p.capsuleBgHover = Rgba(255, 255, 255, 0.84f);
        p.capsuleBgPressed = Rgba(241, 245, 249, 0.92f);
        p.capsuleBorder = Rgba(15, 23, 42, 0.085f);
        p.capsuleHighlight = Rgba(255, 255, 255, 0.55f);
        p.divider = Rgba(15, 23, 42, 0.08f);
        p.trackBg = Rgba(15, 23, 42, 0.09f);

        if (minimal) {
            p.label = Rgba(65, 75, 90, 0.78f);
            p.labelStrong = Rgba(24, 32, 44, 1.0f);
            p.ok = Rgba(15, 118, 110, 1.0f);
            p.okMem = Rgba(3, 105, 161, 1.0f);
            p.warn = Rgba(180, 83, 9, 1.0f);
            p.hot = Rgba(185, 28, 28, 1.0f);
            p.freq = Rgba(29, 78, 216, 1.0f);
            p.netUp = Rgba(2, 132, 199, 1.0f);
            p.netDown = Rgba(13, 148, 136, 1.0f);
            p.netIdle = Rgba(71, 85, 105, 0.66f);
        } else {
            p.label = Rgba(78, 88, 102, 1.0f);
            p.labelStrong = Rgba(24, 32, 44, 1.0f);
            p.ok = Rgba(12, 122, 102, 1.0f);
            p.okMem = Rgba(14, 106, 138, 1.0f);
            p.warn = Rgba(196, 106, 0, 1.0f);
            p.hot = Rgba(202, 44, 40, 1.0f);
            p.freq = Rgba(30, 82, 152, 1.0f);
            p.netUp = Rgba(0, 103, 192, 1.0f);
            p.netDown = Rgba(11, 122, 114, 1.0f);
            p.netIdle = Rgba(100, 112, 128, 1.0f);
        }

        p.flyBg = Rgba(248, 250, 252, 0.96f);
        p.flyBorder = Rgba(15, 23, 42, 0.12f);
        p.cardBg = Rgba(255, 255, 255, 0.92f);
        p.cardBorder = Rgba(15, 23, 42, 0.07f);
        p.btnBg = Rgba(226, 232, 240, 0.70f);
        p.btnHover = Rgba(203, 213, 225, 0.90f);
        p.btnText = Rgba(30, 41, 59, 1.0f);
    } else {
        p.capsuleBg = Rgba(255, 255, 255, 0.075f);
        p.capsuleBgHover = Rgba(255, 255, 255, 0.135f);
        p.capsuleBgPressed = Rgba(255, 255, 255, 0.05f);
        p.capsuleBorder = Rgba(255, 255, 255, 0.11f);
        p.capsuleHighlight = Rgba(255, 255, 255, 0.065f);
        p.divider = Rgba(255, 255, 255, 0.10f);
        p.trackBg = Rgba(255, 255, 255, 0.11f);

        if (minimal) {
            p.label = Rgba(195, 204, 218, 0.72f);
            p.labelStrong = Rgba(241, 245, 249, 1.0f);
            p.ok = Rgba(110, 231, 192, 1.0f);
            p.okMem = Rgba(125, 211, 252, 1.0f);
            p.warn = Rgba(251, 191, 36, 1.0f);
            p.hot = Rgba(248, 113, 113, 1.0f);
            p.freq = Rgba(147, 197, 253, 1.0f);
            p.netUp = Rgba(124, 196, 250, 1.0f);
            p.netDown = Rgba(110, 231, 192, 1.0f);
            p.netIdle = Rgba(195, 204, 218, 0.60f);
        } else {
            p.label = Rgba(166, 176, 190, 1.0f);
            p.labelStrong = Rgba(241, 245, 249, 1.0f);
            p.ok = Rgba(74, 214, 168, 1.0f);
            p.okMem = Rgba(92, 198, 250, 1.0f);
            p.warn = Rgba(255, 184, 77, 1.0f);
            p.hot = Rgba(255, 107, 107, 1.0f);
            p.freq = Rgba(125, 196, 255, 1.0f);
            p.netUp = Rgba(96, 180, 255, 1.0f);
            p.netDown = Rgba(79, 209, 165, 1.0f);
            p.netIdle = Rgba(142, 152, 168, 1.0f);
        }

        p.flyBg = Rgba(30, 34, 42, 0.96f);
        p.flyBorder = Rgba(255, 255, 255, 0.12f);
        p.cardBg = Rgba(42, 47, 57, 0.90f);
        p.cardBorder = Rgba(255, 255, 255, 0.08f);
        p.btnBg = Rgba(255, 255, 255, 0.09f);
        p.btnHover = Rgba(255, 255, 255, 0.17f);
        p.btnText = Rgba(241, 245, 249, 1.0f);
    }
    return p;
}

inline D2D1_COLOR_F PickLoadColor(double pct, const Palette& p, bool memStyle = false) {
    double hotThresh = memStyle ? 90.0 : 88.0;
    double warnThresh = memStyle ? 78.0 : 70.0;
    if (pct >= hotThresh) return p.hot;
    if (pct >= warnThresh) return p.warn;
    return memStyle ? p.okMem : p.ok;
}

inline D2D1_COLOR_F PickTempColor(double tempC, const Palette& p) {
    if (tempC >= 80.0) return p.hot;
    if (tempC >= 65.0) return p.warn;
    return p.ok;
}

inline void FormatSpeedCompact(double bytesPerSec, wchar_t* buf, size_t cap, bool withPerSec) {
    double kbps = bytesPerSec / 1024.0;
    const wchar_t* suffix = withPerSec ? L"/s" : L"";
    if (kbps < 1000.0) {
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.1fKB%ls", kbps, suffix);
    } else if (kbps < 1000.0 * 1024.0) {
        double mbps = kbps / 1024.0;
        if (mbps >= 100.0) _snwprintf_s(buf, cap, _TRUNCATE, L"%.0fMB%ls", mbps, suffix);
        else _snwprintf_s(buf, cap, _TRUNCATE, L"%.1fMB%ls", mbps, suffix);
    } else {
        double gbps = kbps / (1024.0 * 1024.0);
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.2fGB%ls", gbps, suffix);
    }
}

inline void FormatBytesShort(uint64_t bytes, wchar_t* buf, size_t cap) {
    double b = static_cast<double>(bytes);
    if (b < 1024.0 * 1024.0) {
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.1f KB", b / 1024.0);
    } else if (b < 1024.0 * 1024.0 * 1024.0) {
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.1f MB", b / (1024.0 * 1024.0));
    } else {
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
    }
}
