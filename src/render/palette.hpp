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

    // Pure neutral zinc ink hierarchy
    D2D1_COLOR_F labelStrong{}; // Primary ink (#141417 / #f4f4f6)
    D2D1_COLOR_F label{};       // Secondary ink (#5c5e66 / #9b9da6)
    D2D1_COLOR_F muted{};       // Muted unit / meta ink (#8c8f99 / #686b75)

    // Semantic status colors
    D2D1_COLOR_F ok{};
    D2D1_COLOR_F okMem{};
    D2D1_COLOR_F warn{};
    D2D1_COLOR_F hot{};
    D2D1_COLOR_F freq{};
    D2D1_COLOR_F gold{};

    // Restrained signal accents for charts, ticks, and arrows
    D2D1_COLOR_F accentCpu{};
    D2D1_COLOR_F accentGpu{};
    D2D1_COLOR_F accentVram{};
    D2D1_COLOR_F accentRam{};
    D2D1_COLOR_F netUp{};
    D2D1_COLOR_F netDown{};
    D2D1_COLOR_F netIdle{};

    // Flyout / B1 Swiss surface tokens
    D2D1_COLOR_F flyBg{};
    D2D1_COLOR_F flyBorder{};
    D2D1_COLOR_F flyHighlight{};
    D2D1_COLOR_F rowHover{};
    D2D1_COLOR_F iconWell{};
    D2D1_COLOR_F wellBg{};
    D2D1_COLOR_F segBg{};
    D2D1_COLOR_F segActive{};
    D2D1_COLOR_F segActiveBorder{};

    // Pill badges
    D2D1_COLOR_F badgeNeutralBg{};
    D2D1_COLOR_F badgeNeutralFg{};
    D2D1_COLOR_F badgeWarnBg{};
    D2D1_COLOR_F badgeWarnFg{};
    D2D1_COLOR_F badgeHotBg{};
    D2D1_COLOR_F badgeHotFg{};
};

inline Palette MakePalette(bool light, bool minimal = false) {
    Palette p;
    p.light = light;
    if (light) {
        p.capsuleBg = Rgba(255, 255, 255, 0.56f);
        p.capsuleBgHover = Rgba(255, 255, 255, 0.80f);
        p.capsuleBgPressed = Rgba(244, 244, 247, 0.90f);
        p.capsuleBorder = Rgba(20, 20, 24, 0.085f);
        p.capsuleHighlight = Rgba(255, 255, 255, 0.62f);
        p.divider = Rgba(20, 20, 24, 0.078f);
        p.trackBg = Rgba(20, 20, 24, 0.085f);

        p.labelStrong = Rgba(20, 20, 23, 1.0f);
        p.label = minimal ? Rgba(60, 64, 74, 0.72f) : Rgba(92, 94, 102, 1.0f);
        p.muted = Rgba(136, 139, 150, 1.0f);

        p.ok = Rgba(16, 185, 129, 1.0f);
        p.okMem = Rgba(2, 132, 199, 1.0f);
        p.warn = Rgba(217, 119, 6, 1.0f);
        p.hot = Rgba(220, 38, 38, 1.0f);
        p.freq = Rgba(92, 94, 102, 1.0f);
        p.gold = Rgba(176, 116, 6, 1.0f);

        p.accentCpu = Rgba(13, 148, 136, 1.0f);
        p.accentGpu = Rgba(37, 99, 235, 1.0f);
        p.accentVram = Rgba(99, 102, 241, 1.0f);
        p.accentRam = Rgba(2, 132, 199, 1.0f);
        p.netUp = Rgba(37, 99, 235, 1.0f);
        p.netDown = Rgba(5, 150, 105, 1.0f);
        p.netIdle = Rgba(92, 94, 102, 0.68f);

        p.flyBg = Rgba(251, 251, 252, 0.94f);
        p.flyBorder = Rgba(20, 20, 24, 0.105f);
        p.flyHighlight = Rgba(255, 255, 255, 0.85f);
        p.rowHover = Rgba(20, 20, 24, 0.042f);
        p.iconWell = Rgba(20, 20, 24, 0.052f);
        p.wellBg = Rgba(20, 20, 24, 0.038f);
        p.segBg = Rgba(20, 20, 24, 0.058f);
        p.segActive = Rgba(255, 255, 255, 0.98f);
        p.segActiveBorder = Rgba(20, 20, 24, 0.07f);

        p.badgeNeutralBg = Rgba(20, 20, 24, 0.055f);
        p.badgeNeutralFg = Rgba(88, 90, 98, 1.0f);
        p.badgeWarnBg = Rgba(217, 119, 6, 0.13f);
        p.badgeWarnFg = Rgba(180, 83, 9, 1.0f);
        p.badgeHotBg = Rgba(220, 38, 38, 0.13f);
        p.badgeHotFg = Rgba(185, 28, 28, 1.0f);
    } else {
        p.capsuleBg = Rgba(255, 255, 255, 0.072f);
        p.capsuleBgHover = Rgba(255, 255, 255, 0.125f);
        p.capsuleBgPressed = Rgba(255, 255, 255, 0.045f);
        p.capsuleBorder = Rgba(255, 255, 255, 0.105f);
        p.capsuleHighlight = Rgba(255, 255, 255, 0.07f);
        p.divider = Rgba(255, 255, 255, 0.082f);
        p.trackBg = Rgba(255, 255, 255, 0.098f);

        p.labelStrong = Rgba(244, 244, 246, 1.0f);
        p.label = minimal ? Rgba(196, 200, 210, 0.66f) : Rgba(155, 157, 166, 1.0f);
        p.muted = Rgba(110, 113, 124, 1.0f);

        p.ok = Rgba(52, 211, 153, 1.0f);
        p.okMem = Rgba(56, 189, 248, 1.0f);
        p.warn = Rgba(251, 191, 36, 1.0f);
        p.hot = Rgba(248, 113, 113, 1.0f);
        p.freq = Rgba(155, 157, 166, 1.0f);
        p.gold = Rgba(245, 195, 82, 1.0f);

        p.accentCpu = Rgba(45, 212, 191, 1.0f);
        p.accentGpu = Rgba(96, 165, 250, 1.0f);
        p.accentVram = Rgba(129, 140, 248, 1.0f);
        p.accentRam = Rgba(56, 189, 248, 1.0f);
        p.netUp = Rgba(96, 165, 250, 1.0f);
        p.netDown = Rgba(52, 211, 153, 1.0f);
        p.netIdle = Rgba(168, 172, 182, 0.62f);

        p.flyBg = Rgba(26, 27, 31, 0.94f);
        p.flyBorder = Rgba(255, 255, 255, 0.105f);
        p.flyHighlight = Rgba(255, 255, 255, 0.11f);
        p.rowHover = Rgba(255, 255, 255, 0.052f);
        p.iconWell = Rgba(255, 255, 255, 0.068f);
        p.wellBg = Rgba(255, 255, 255, 0.052f);
        p.segBg = Rgba(255, 255, 255, 0.065f);
        p.segActive = Rgba(255, 255, 255, 0.16f);
        p.segActiveBorder = Rgba(255, 255, 255, 0.09f);

        p.badgeNeutralBg = Rgba(255, 255, 255, 0.072f);
        p.badgeNeutralFg = Rgba(168, 172, 182, 1.0f);
        p.badgeWarnBg = Rgba(251, 191, 36, 0.15f);
        p.badgeWarnFg = Rgba(252, 211, 77, 1.0f);
        p.badgeHotBg = Rgba(248, 113, 113, 0.17f);
        p.badgeHotFg = Rgba(252, 165, 165, 1.0f);
    }
    return p;
}

// Neutral-first load color for primary numeric readouts: calm ink at normal load, semantic color on warn/hot.
inline D2D1_COLOR_F PickLoadColor(double pct, const Palette& p, bool memStyle = false) {
    double hotThresh = memStyle ? 90.0 : 88.0;
    double warnThresh = memStyle ? 78.0 : 70.0;
    if (pct >= hotThresh) return p.hot;
    if (pct >= warnThresh) return p.warn;
    return p.labelStrong;
}

// Signal color for bars, histograms, and segmented meters.
inline D2D1_COLOR_F PickSignalColor(double pct, const D2D1_COLOR_F& normalSignal, const Palette& p, bool memStyle = false) {
    double hotThresh = memStyle ? 90.0 : 88.0;
    double warnThresh = memStyle ? 78.0 : 70.0;
    if (pct >= hotThresh) return p.hot;
    if (pct >= warnThresh) return p.warn;
    return normalSignal;
}

// Neutral-first temperature color for text readouts (subtle secondary ink when cool, amber/red when warm/hot).
inline D2D1_COLOR_F PickTempColor(double tempC, const Palette& p) {
    if (tempC >= 80.0) return p.hot;
    if (tempC >= 66.0) return p.warn;
    return p.label;
}

inline void PickTempBadgeColors(double tempC, const Palette& p, D2D1_COLOR_F& outBg, D2D1_COLOR_F& outFg) {
    if (tempC >= 80.0) {
        outBg = p.badgeHotBg;
        outFg = p.badgeHotFg;
    } else if (tempC >= 66.0) {
        outBg = p.badgeWarnBg;
        outFg = p.badgeWarnFg;
    } else {
        outBg = p.badgeNeutralBg;
        outFg = p.badgeNeutralFg;
    }
}

inline void FormatSpeedCompact(double bytesPerSec, wchar_t* buf, size_t cap, bool withPerSec) {
    double kbps = bytesPerSec / 1024.0;
    const wchar_t* suffix = withPerSec ? L"/s" : L"";
    if (kbps < 1000.0) {
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.1f%ls%ls", kbps, withPerSec ? L" KB" : L"K", suffix);
    } else if (kbps < 1000.0 * 1024.0) {
        double mbps = kbps / 1024.0;
        if (mbps >= 100.0) _snwprintf_s(buf, cap, _TRUNCATE, L"%.0f%ls%ls", mbps, withPerSec ? L" MB" : L"M", suffix);
        else _snwprintf_s(buf, cap, _TRUNCATE, L"%.1f%ls%ls", mbps, withPerSec ? L" MB" : L"M", suffix);
    } else {
        double gbps = kbps / (1024.0 * 1024.0);
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.2f%ls%ls", gbps, withPerSec ? L" GB" : L"G", suffix);
    }
}

inline void FormatBytesShort(uint64_t bytes, wchar_t* buf, size_t cap) {
    double b = static_cast<double>(bytes);
    if (b < 1024.0 * 1024.0) {
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.0f KB", b / 1024.0);
    } else if (b < 1024.0 * 1024.0 * 1024.0) {
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.1f MB", b / (1024.0 * 1024.0));
    } else {
        _snwprintf_s(buf, cap, _TRUNCATE, L"%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
    }
}
