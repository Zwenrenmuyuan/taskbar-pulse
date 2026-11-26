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

#include <algorithm>
#include <cmath>

namespace anim {

// Frame-rate independent exponential smoothing. Returns true while still moving.
inline bool Damp(float& current, float target, float speed, float dt, float eps = 0.002f) {
    float diff = target - current;
    if (std::fabs(diff) <= eps) {
        if (current != target) {
            current = target;
            return true;
        }
        return false;
    }
    float factor = 1.0f - std::exp(-speed * dt);
    current += diff * factor;
    if (std::fabs(target - current) <= eps) {
        current = target;
    }
    return true;
}

inline bool DampDouble(double& current, double target, float speed, float dt, double eps = 0.05) {
    double diff = target - current;
    if (std::fabs(diff) <= eps) {
        if (current != target) {
            current = target;
            return true;
        }
        return false;
    }
    double factor = 1.0 - std::exp(-static_cast<double>(speed) * static_cast<double>(dt));
    current += diff * factor;
    if (std::fabs(target - current) <= eps) {
        current = target;
    }
    return true;
}

inline D2D1_COLOR_F LerpColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    float k = std::clamp(t, 0.0f, 1.0f);
    return D2D1::ColorF(
        a.r + (b.r - a.r) * k,
        a.g + (b.g - a.g) * k,
        a.b + (b.b - a.b) * k,
        a.a + (b.a - a.a) * k);
}

inline bool DampColor(D2D1_COLOR_F& current, const D2D1_COLOR_F& target, float speed, float dt, float eps = 0.004f) {
    bool moving = false;
    moving |= Damp(current.r, target.r, speed, dt, eps);
    moving |= Damp(current.g, target.g, speed, dt, eps);
    moving |= Damp(current.b, target.b, speed, dt, eps);
    moving |= Damp(current.a, target.a, speed, dt, eps);
    return moving;
}

} // namespace anim

struct PopupMotionSpec {
    float fromOpacity = 0.0f;
    float toOpacity = 1.0f;
    float fromOffsetXDip = 0.0f;
    float toOffsetXDip = 0.0f;
    float fromOffsetYDip = 0.0f;
    float toOffsetYDip = 0.0f;
    float fromScale = 1.0f;
    float toScale = 1.0f;
    float centerXRatio = 0.5f;
    float centerYRatio = 0.5f;
    float durationSec = 0.16f;
    bool easeOut = true;
};

struct AnimatedMetrics {
    bool initialized = false;

    double cpuPct = 0.0;
    double gpuPct = 0.0;
    double vramPct = 0.0;
    double ramPct = 0.0;
    double netLogPct = 0.0;

    // Smoothly damped chart vertical ceilings to avoid vertical snap on spikes
    double cpuSparkCeil = 28.0;
    double gpuSparkCeil = 28.0;
    double netFlyoutMax = 10240.0;

    // 1.0 right after a new sample arrives -> smoothly settles to 0.0 (horizontal glide)
    float historySlide = 0.0f;

    // Smoothly interpolated semantic colors
    bool colorsInitialized = false;
    bool lastLightTheme = true;
    bool lastMinimalStyle = false;
    D2D1_COLOR_F cpuCol{};
    D2D1_COLOR_F gpuCol{};
    D2D1_COLOR_F vramCol{};
    D2D1_COLOR_F ramCol{};
    D2D1_COLOR_F cpuTempCol{};
    D2D1_COLOR_F gpuTempCol{};
};

struct FlyoutAnimState {
    float btnHover[4]{};
    float btnPress[4]{};
};

struct MenuAnimState {
    float pillY = 0.0f;
    float pillAlpha = 0.0f;
    float pillDanger = 0.0f;
    bool hasPillPos = false;
};
