#include "render/draw_primitives.hpp"
#include "render/animator.hpp"
#include <cmath>

void DrawVectorArrow(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, bool up, ID2D1Brush* brush) {
    float halfH = 3.4f;
    float headW = 2.4f;
    float headH = 2.3f;
    if (up) {
        rt->DrawLine(D2D1::Point2F(cx, cy + halfH), D2D1::Point2F(cx, cy - halfH), brush, 1.3f, roundStroke);
        rt->DrawLine(D2D1::Point2F(cx - headW, cy - halfH + headH), D2D1::Point2F(cx, cy - halfH), brush, 1.3f, roundStroke);
        rt->DrawLine(D2D1::Point2F(cx + headW, cy - halfH + headH), D2D1::Point2F(cx, cy - halfH), brush, 1.3f, roundStroke);
    } else {
        rt->DrawLine(D2D1::Point2F(cx, cy - halfH), D2D1::Point2F(cx, cy + halfH), brush, 1.3f, roundStroke);
        rt->DrawLine(D2D1::Point2F(cx - headW, cy + halfH - headH), D2D1::Point2F(cx, cy + halfH), brush, 1.3f, roundStroke);
        rt->DrawLine(D2D1::Point2F(cx + headW, cy + halfH - headH), D2D1::Point2F(cx, cy + halfH), brush, 1.3f, roundStroke);
    }
}

void DrawMiniBar(
    ID2D1RenderTarget* rt,
    float x,
    float y,
    float w,
    float h,
    double pct,
    ID2D1Brush* trackBrush,
    ID2D1Brush* fillBrush) {
    float r = h * 0.5f;
    D2D1_ROUNDED_RECT track = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r);
    rt->FillRoundedRectangle(&track, trackBrush);

    float clamped = static_cast<float>(std::clamp(pct / 100.0, 0.0, 1.0));
    if (clamped > 0.005f) {
        float fillW = std::max(h * 2.4f, w * clamped);
        fillW = std::min(fillW, w);
        D2D1_ROUNDED_RECT fill = D2D1::RoundedRect(D2D1::RectF(x, y, x + fillW, y + h), r, r);
        rt->FillRoundedRectangle(&fill, fillBrush);
    }
}

void DrawSegmentedMeter(
    ID2D1RenderTarget* rt,
    float x,
    float y,
    float w,
    float h,
    double pct,
    int segments,
    const D2D1_COLOR_F& trackCol,
    const D2D1_COLOR_F& fillCol,
    ID2D1SolidColorBrush* brush) {
    if (!rt || !brush || segments <= 0 || w <= 4.0f) return;
    const float gap = 2.4f;
    const float segW = (w - gap * static_cast<float>(segments - 1)) / static_cast<float>(segments);
    if (segW <= 0.8f) return;
    const float rad = std::min(1.6f, segW * 0.45f);
    const float exactLit = static_cast<float>(std::clamp(pct / 100.0, 0.0, 1.0) * static_cast<double>(segments));

    for (int i = 0; i < segments; ++i) {
        float sx = x + static_cast<float>(i) * (segW + gap);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(sx, y, sx + segW, y + h), rad, rad);

        float frac = std::clamp(exactLit - static_cast<float>(i), 0.0f, 1.0f);
        if (i == 0 && pct > 0.1 && frac < 1.0f) frac = 1.0f;

        if (frac >= 0.99f) {
            brush->SetColor(fillCol);
            rt->FillRoundedRectangle(&rr, brush);
        } else if (frac <= 0.01f) {
            brush->SetColor(trackCol);
            rt->FillRoundedRectangle(&rr, brush);
        } else {
            // Layer translucent vibrant fill over track so partial tick never turns muddy gray
            brush->SetColor(trackCol);
            rt->FillRoundedRectangle(&rr, brush);
            D2D1_COLOR_F partial = fillCol;
            partial.a *= frac;
            brush->SetColor(partial);
            rt->FillRoundedRectangle(&rr, brush);
        }
    }
}

void DrawToggleSwitch(
    ID2D1RenderTarget* rt,
    float x,
    float y,
    float w,
    float h,
    float progress,
    const D2D1_COLOR_F& offTrack,
    const D2D1_COLOR_F& onTrack,
    const D2D1_COLOR_F& offThumb,
    const D2D1_COLOR_F& onThumb,
    ID2D1SolidColorBrush* brush) {
    if (!rt || !brush) return;
    const float t = std::clamp(progress, 0.0f, 1.0f);
    const float r = h * 0.5f;

    brush->SetColor(anim::LerpColor(offTrack, onTrack, t));
    D2D1_ROUNDED_RECT track = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r);
    rt->FillRoundedRectangle(&track, brush);

    const float pad = 2.5f;
    const float thumbR = (h - pad * 2.0f) * 0.5f;
    const float minCx = x + pad + thumbR;
    const float maxCx = x + w - pad - thumbR;
    const float cx = minCx + (maxCx - minCx) * t;
    const float cy = y + r;

    brush->SetColor(anim::LerpColor(offThumb, onThumb, t));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), thumbR, thumbR), brush);
}

void DrawCpuIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush) {
    if (!rt || !brush) return;
    // Outer die body
    D2D1_ROUNDED_RECT body = D2D1::RoundedRect(D2D1::RectF(cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 4.0f), 1.4f, 1.4f);
    rt->DrawRoundedRectangle(&body, brush, 1.25f, roundStroke);
    // Inner core
    D2D1_ROUNDED_RECT core = D2D1::RoundedRect(D2D1::RectF(cx - 1.6f, cy - 1.6f, cx + 1.6f, cy + 1.6f), 0.5f, 0.5f);
    rt->DrawRoundedRectangle(&core, brush, 1.05f, roundStroke);
    // 8 micro pins
    const float p1 = 4.2f;
    const float p2 = 6.1f;
    const float off = 1.8f;
    rt->DrawLine(D2D1::Point2F(cx - off, cy - p1), D2D1::Point2F(cx - off, cy - p2), brush, 1.15f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx + off, cy - p1), D2D1::Point2F(cx + off, cy - p2), brush, 1.15f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx - off, cy + p1), D2D1::Point2F(cx - off, cy + p2), brush, 1.15f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx + off, cy + p1), D2D1::Point2F(cx + off, cy + p2), brush, 1.15f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx - p1, cy - off), D2D1::Point2F(cx - p2, cy - off), brush, 1.15f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx - p1, cy + off), D2D1::Point2F(cx - p2, cy + off), brush, 1.15f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx + p1, cy - off), D2D1::Point2F(cx + p2, cy - off), brush, 1.15f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx + p1, cy + off), D2D1::Point2F(cx + p2, cy + off), brush, 1.15f, roundStroke);
}

void DrawGpuIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush) {
    if (!rt || !brush) return;
    // Card shroud
    D2D1_ROUNDED_RECT shroud = D2D1::RoundedRect(D2D1::RectF(cx - 6.0f, cy - 4.0f, cx + 6.0f, cy + 4.0f), 1.7f, 1.7f);
    rt->DrawRoundedRectangle(&shroud, brush, 1.25f, roundStroke);
    // Fan circle on left
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 1.8f, cy), 2.1f, 2.1f), brush, 1.15f, roundStroke);
    // Heatsink fins on right
    rt->DrawLine(D2D1::Point2F(cx + 2.3f, cy - 1.8f), D2D1::Point2F(cx + 2.3f, cy + 1.8f), brush, 1.15f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx + 4.0f, cy - 1.2f), D2D1::Point2F(cx + 4.0f, cy + 1.2f), brush, 1.15f, roundStroke);
}

void DrawNetIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush) {
    if (!rt || !brush) return;
    // Up arrow left
    float lx = cx - 2.7f;
    rt->DrawLine(D2D1::Point2F(lx, cy + 4.2f), D2D1::Point2F(lx, cy - 4.2f), brush, 1.25f, roundStroke);
    rt->DrawLine(D2D1::Point2F(lx - 2.2f, cy - 1.9f), D2D1::Point2F(lx, cy - 4.2f), brush, 1.25f, roundStroke);
    rt->DrawLine(D2D1::Point2F(lx + 2.2f, cy - 1.9f), D2D1::Point2F(lx, cy - 4.2f), brush, 1.25f, roundStroke);
    // Down arrow right
    float rx = cx + 2.7f;
    rt->DrawLine(D2D1::Point2F(rx, cy - 4.2f), D2D1::Point2F(rx, cy + 4.2f), brush, 1.25f, roundStroke);
    rt->DrawLine(D2D1::Point2F(rx - 2.2f, cy + 1.9f), D2D1::Point2F(rx, cy + 4.2f), brush, 1.25f, roundStroke);
    rt->DrawLine(D2D1::Point2F(rx + 2.2f, cy + 1.9f), D2D1::Point2F(rx, cy + 4.2f), brush, 1.25f, roundStroke);
}

void DrawGearIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush) {
    if (!rt || !brush) return;
    // Center ring
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 2.15f, 2.15f), brush, 1.25f, roundStroke);
    // Outer ring
    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.45f, 4.45f), brush, 1.2f, roundStroke);
    // 8 radial teeth
    constexpr float kPi = 3.14159265f;
    for (int i = 0; i < 8; ++i) {
        float ang = static_cast<float>(i) * (kPi * 0.25f);
        float c = std::cos(ang);
        float s = std::sin(ang);
        rt->DrawLine(
            D2D1::Point2F(cx + c * 4.55f, cy + s * 4.55f),
            D2D1::Point2F(cx + c * 6.0f, cy + s * 6.0f),
            brush, 1.45f, roundStroke);
    }
}

void DrawBackChevronIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush) {
    if (!rt || !brush) return;
    rt->DrawLine(D2D1::Point2F(cx + 1.8f, cy - 3.8f), D2D1::Point2F(cx - 2.0f, cy), brush, 1.45f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx - 2.0f, cy), D2D1::Point2F(cx + 1.8f, cy + 3.8f), brush, 1.45f, roundStroke);
}

void DrawCloseXIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush) {
    if (!rt || !brush) return;
    const float r = 3.2f;
    rt->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), brush, 1.4f, roundStroke);
    rt->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx - r, cy + r), brush, 1.4f, roundStroke);
}

bool PresentWicToWindow(HWND hwnd, POINT ptScreen, IWICBitmap* wicBmp, int wPx, int hPx) {
    if (!hwnd || !wicBmp || wPx <= 0 || hPx <= 0) return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = wPx;
    bmi.bmiHeader.biHeight = -hPx; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* dibBits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP hBmp = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
    bool ok = false;

    if (memDc && hBmp && dibBits) {
        HGDIOBJ oldObj = SelectObject(memDc, hBmp);
        WICRect rc{ 0, 0, wPx, hPx };
        UINT stride = static_cast<UINT>(wPx * 4);
        UINT bufSize = stride * static_cast<UINT>(hPx);
        if (SUCCEEDED(wicBmp->CopyPixels(&rc, stride, bufSize, static_cast<BYTE*>(dibBits)))) {
            SIZE sz{ wPx, hPx };
            POINT ptSrc{ 0, 0 };
            BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            ok = UpdateLayeredWindow(hwnd, screenDc, &ptScreen, &sz, memDc, &ptSrc, 0, &bf, ULW_ALPHA) != FALSE;
        }
        SelectObject(memDc, oldObj);
    }

    if (hBmp) DeleteObject(hBmp);
    if (memDc) DeleteDC(memDc);
    if (screenDc) ReleaseDC(nullptr, screenDc);
    return ok;
}

void DrawCardShadow(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brShadow,
    float padX,
    float padTop,
    float innerW,
    float innerH,
    float cornerR,
    bool light) {
    if (!brShadow) return;
    // Soft diffused 7-layer Fluent elevation shadow
    constexpr int kLayers = 7;
    for (int s = kLayers; s >= 1; --s) {
        float t = static_cast<float>(s) / static_cast<float>(kLayers);
        float ex = static_cast<float>(s) * 1.15f;
        float dy = 1.2f + static_cast<float>(s) * 0.42f;
        float falloff = (1.0f - t * 0.86f);
        float alpha = (light ? 0.036f : 0.115f) * falloff * falloff;
        brShadow->SetColor(Rgba(8, 12, 22, alpha));
        D2D1_ROUNDED_RECT sr = D2D1::RoundedRect(
            D2D1::RectF(padX - ex + 0.5f, padTop - ex * 0.65f + dy, padX + innerW + ex - 0.5f, padTop + innerH + ex + dy),
            cornerR + ex * 0.55f, cornerR + ex * 0.55f);
        rt->FillRoundedRectangle(&sr, brShadow);
    }
}

ID2D1StrokeStyle* CreateRoundStrokeStyle(ID2D1Factory* factory) {
    ID2D1StrokeStyle* style = nullptr;
    D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND);
    factory->CreateStrokeStyle(&sp, nullptr, 0, &style);
    return style;
}

void DrawMenuCheckIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* style, float cx, float cy, ID2D1Brush* brush) {
    rt->DrawLine(D2D1::Point2F(cx - 3.6f, cy + 0.2f), D2D1::Point2F(cx - 1.1f, cy + 2.9f), brush, 1.5f, style);
    rt->DrawLine(D2D1::Point2F(cx - 1.1f, cy + 2.9f), D2D1::Point2F(cx + 3.8f, cy - 2.6f), brush, 1.5f, style);
}

void DrawMenuChevronRight(ID2D1RenderTarget* rt, ID2D1StrokeStyle* style, float cx, float cy, ID2D1Brush* brush) {
    rt->DrawLine(D2D1::Point2F(cx - 1.8f, cy - 3.3f), D2D1::Point2F(cx + 1.6f, cy), brush, 1.35f, style);
    rt->DrawLine(D2D1::Point2F(cx + 1.6f, cy), D2D1::Point2F(cx - 1.8f, cy + 3.3f), brush, 1.35f, style);
}
