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
#include <wincodec.h>

#include "render/palette.hpp"
#include "collectors/sensors.hpp"
#include <algorithm>
#include <cmath>

void DrawVectorArrow(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, bool up, ID2D1Brush* brush);
void DrawMiniBar(ID2D1RenderTarget* rt, float x, float y, float w, float h, double pct, ID2D1Brush* trackBrush, ID2D1Brush* fillBrush);
void DrawSegmentedMeter(ID2D1RenderTarget* rt, float x, float y, float w, float h, double pct, int segments,
    const D2D1_COLOR_F& trackCol, const D2D1_COLOR_F& fillCol, ID2D1SolidColorBrush* brush,
    float gap = 2.4f, bool forceFirstLit = false);
void DrawToggleSwitch(ID2D1RenderTarget* rt, float x, float y, float w, float h, float progress,
    const D2D1_COLOR_F& offTrack, const D2D1_COLOR_F& onTrack, const D2D1_COLOR_F& offThumb,
    const D2D1_COLOR_F& onThumb, ID2D1SolidColorBrush* brush);

void DrawCpuIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush, float scale = 1.0f);
void DrawGpuIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush, float scale = 1.0f);
void DrawGoldIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush, float scale = 1.0f);
void DrawNetIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush);
void DrawGearIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush);
void DrawBackChevronIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush);
void DrawCloseXIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, ID2D1Brush* brush);

bool PresentWicToWindow(HWND hwnd, POINT ptScreen, IWICBitmap* wicBmp, int wPx, int hPx);
void DrawCardShadow(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float padX, float padTop, float innerW, float innerH, float cornerR, bool light);
ID2D1StrokeStyle* CreateRoundStrokeStyle(ID2D1Factory* factory);
void DrawMenuCheckIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* style, float cx, float cy, ID2D1Brush* brush);
void DrawMenuChevronRight(ID2D1RenderTarget* rt, ID2D1StrokeStyle* style, float cx, float cy, ID2D1Brush* brush);

// Swiss 22-bar precision micro-histogram sparkline (used in B1 CPU / GPU rows)
template <typename Getter>
void DrawMicroBarHistogram(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brush,
    const D2D1_RECT_F& rect,
    size_t count,
    Getter&& getVal,
    double maxVal,
    const D2D1_COLOR_F& baseColor,
    float slideOffset = 0.0f) {
    if (!rt || !brush) return;
    constexpr size_t kBars = 22;
    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    if (w <= 4.0f || h <= 4.0f) return;

    const float gap = 2.1f;
    const float bw = (w - gap * static_cast<float>(kBars - 1)) / static_cast<float>(kBars);
    if (bw <= 0.8f) return;
    const float rad = std::min(1.6f, bw * 0.45f);
    const float step = bw + gap;
    const float shiftX = (count > 2) ? (std::clamp(slideOffset, 0.0f, 1.0f) * step) : 0.0f;

    const double ceilVal = std::max(18.0, maxVal);
    const size_t n = std::min(count, kBars);
    const size_t start = (count > n) ? (count - n) : 0;
    const size_t offset = kBars - n;

    rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Draw subtle resting baseline slots for unfilled history slots
    D2D1_COLOR_F emptyCol = baseColor;
    emptyCol.a = 0.14f;
    brush->SetColor(emptyCol);
    for (size_t s = 0; s < offset; ++s) {
        float x = rect.left + static_cast<float>(s) * step;
        float bh = 2.8f;
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, rect.bottom - bh, x + bw, rect.bottom), rad, rad);
        rt->FillRoundedRectangle(&rr, brush);
    }

    for (size_t i = 0; i < n; ++i) {
        size_t slot = offset + i;
        float x = rect.left + static_cast<float>(slot) * step + shiftX;
        double v = static_cast<double>(getVal(start + i));
        float norm = static_cast<float>(std::clamp(v / ceilVal, 0.08, 1.0));
        float bh = std::max(3.0f, norm * (h - 1.5f));
        float tAge = static_cast<float>(slot) / static_cast<float>(kBars - 1);
        float alpha = (i + 1 == n) ? 1.0f : (0.24f + tAge * 0.68f);

        D2D1_COLOR_F col = baseColor;
        col.a *= alpha;
        brush->SetColor(col);

        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(x, rect.bottom - bh, x + bw, rect.bottom), rad, rad);
        rt->FillRoundedRectangle(&rr, brush);
    }

    rt->PopAxisAlignedClip();
}

template <typename Getter>
void DrawSparkline(
    ID2D1RenderTarget* rt,
    ID2D1Factory* factory,
    ID2D1StrokeStyle* roundStroke,
    ID2D1SolidColorBrush* sharedBrush,
    const D2D1_RECT_F& rect,
    size_t count,
    Getter&& getVal,
    double maxVal,
    const D2D1_COLOR_F& strokeColor,
    bool drawGrid = true,
    float slideOffset = 0.0f,
    bool drawTipDot = true) {
    if (drawGrid && sharedBrush) {
        sharedBrush->SetColor(Rgba(128, 132, 144, 0.12f));
        float midY = (rect.top + rect.bottom) * 0.5f;
        rt->DrawLine(D2D1::Point2F(rect.left, midY), D2D1::Point2F(rect.right, midY), sharedBrush, 0.75f);
        rt->DrawLine(D2D1::Point2F(rect.left, rect.bottom - 0.5f), D2D1::Point2F(rect.right, rect.bottom - 0.5f), sharedBrush, 0.75f);
    }
    if (count < 2 || maxVal <= 0.0) return;

    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    if (w <= 2.0f || h <= 2.0f) return;

    constexpr size_t kWindow = 28;
    const size_t n = std::min(count, kWindow);
    const size_t start = (count > n) ? (count - n) : 0;
    const size_t offset = kWindow - n;
    const float slotStep = w / static_cast<float>(kWindow - 1);
    const float shiftX = (n > 2) ? (std::clamp(slideOffset, 0.0f, 1.0f) * slotStep) : 0.0f;

    D2D1_POINT_2F pts[kWindow]{};
    for (size_t i = 0; i < n; ++i) {
        float x = rect.left + static_cast<float>(offset + i) * slotStep + shiftX;
        float norm = static_cast<float>(std::clamp(static_cast<double>(getVal(start + i)) / maxVal, 0.03, 1.0));
        float y = rect.bottom - norm * (h - 3.5f) - 1.5f;
        pts[i] = D2D1::Point2F(x, y);
    }

    ID2D1PathGeometry* lineGeo = nullptr;
    ID2D1PathGeometry* areaGeo = nullptr;
    if (FAILED(factory->CreatePathGeometry(&lineGeo))) return;
    if (FAILED(factory->CreatePathGeometry(&areaGeo))) {
        lineGeo->Release();
        return;
    }

    ID2D1GeometrySink* lineSink = nullptr;
    ID2D1GeometrySink* areaSink = nullptr;
    if (SUCCEEDED(lineGeo->Open(&lineSink)) && SUCCEEDED(areaGeo->Open(&areaSink))) {
        lineSink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
        areaSink->BeginFigure(D2D1::Point2F(pts[0].x, rect.bottom), D2D1_FIGURE_BEGIN_FILLED);
        areaSink->AddLine(pts[0]);

        const float minY = rect.top + 1.0f;
        const float maxY = rect.bottom - 1.0f;
        for (size_t i = 0; i + 1 < n; ++i) {
            const D2D1_POINT_2F& p0 = pts[(i > 0) ? (i - 1) : 0];
            const D2D1_POINT_2F& p1 = pts[i];
            const D2D1_POINT_2F& p2 = pts[i + 1];
            const D2D1_POINT_2F& p3 = pts[(i + 2 < n) ? (i + 2) : (n - 1)];

            D2D1_BEZIER_SEGMENT seg{
                D2D1::Point2F(
                    p1.x + (p2.x - p0.x) / 6.0f,
                    std::clamp(p1.y + (p2.y - p0.y) / 6.0f, minY, maxY)),
                D2D1::Point2F(
                    p2.x - (p3.x - p1.x) / 6.0f,
                    std::clamp(p2.y - (p3.y - p1.y) / 6.0f, minY, maxY)),
                p2
            };
            lineSink->AddBezier(seg);
            areaSink->AddBezier(seg);
        }
        areaSink->AddLine(D2D1::Point2F(pts[n - 1].x, rect.bottom));
        lineSink->EndFigure(D2D1_FIGURE_END_OPEN);
        areaSink->EndFigure(D2D1_FIGURE_END_CLOSED);
        lineSink->Close();
        areaSink->Close();
    }
    SafeRelease(lineSink);
    SafeRelease(areaSink);

    rt->PushAxisAlignedClip(D2D1::RectF(rect.left - 1.0f, rect.top - 1.0f, rect.right + 1.5f, rect.bottom + 1.0f),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    D2D1_GRADIENT_STOP stops[2] = {
        { 0.0f, D2D1::ColorF(strokeColor.r, strokeColor.g, strokeColor.b, 0.22f) },
        { 1.0f, D2D1::ColorF(strokeColor.r, strokeColor.g, strokeColor.b, 0.00f) },
    };
    ID2D1GradientStopCollection* stopCol = nullptr;
    if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, &stopCol)) && stopCol) {
        ID2D1LinearGradientBrush* areaBrush = nullptr;
        if (SUCCEEDED(rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(rect.left, rect.top),
                    D2D1::Point2F(rect.left, rect.bottom)),
                stopCol, &areaBrush)) && areaBrush) {
            rt->FillGeometry(areaGeo, areaBrush);
            areaBrush->Release();
        }
        stopCol->Release();
    }

    if (sharedBrush) {
        sharedBrush->SetColor(strokeColor);
        rt->DrawGeometry(lineGeo, sharedBrush, 1.5f, roundStroke);

        if (drawTipDot) {
            const D2D1_POINT_2F& tip = pts[n - 1];
            D2D1_COLOR_F halo = strokeColor;
            halo.a = 0.26f;
            sharedBrush->SetColor(halo);
            rt->FillEllipse(D2D1::Ellipse(tip, 3.6f, 3.6f), sharedBrush);
            sharedBrush->SetColor(strokeColor);
            rt->FillEllipse(D2D1::Ellipse(tip, 1.9f, 1.9f), sharedBrush);
        }
    }

    rt->PopAxisAlignedClip();
    lineGeo->Release();
    areaGeo->Release();
}
