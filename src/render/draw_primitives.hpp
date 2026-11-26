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

void DrawVectorArrow(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, bool up, ID2D1Brush* brush);
void DrawMiniBar(ID2D1RenderTarget* rt, float x, float y, float w, float h, double pct, ID2D1Brush* trackBrush, ID2D1Brush* fillBrush);
bool PresentWicToWindow(HWND hwnd, POINT ptScreen, IWICBitmap* wicBmp, int wPx, int hPx);
void DrawCardShadow(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float padX, float padTop, float innerW, float innerH, float cornerR, bool light);
ID2D1StrokeStyle* CreateRoundStrokeStyle(ID2D1Factory* factory);
void DrawMenuCheckIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* style, float cx, float cy, ID2D1Brush* brush);
void DrawMenuChevronRight(ID2D1RenderTarget* rt, ID2D1StrokeStyle* style, float cx, float cy, ID2D1Brush* brush);

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
    float slideOffset = 0.0f) {
    if (drawGrid && sharedBrush) {
        sharedBrush->SetColor(Rgba(128, 138, 152, 0.14f));
        float midY = (rect.top + rect.bottom) * 0.5f;
        rt->DrawLine(D2D1::Point2F(rect.left, midY), D2D1::Point2F(rect.right, midY), sharedBrush, 0.75f);
        rt->DrawLine(D2D1::Point2F(rect.left, rect.bottom - 0.5f), D2D1::Point2F(rect.right, rect.bottom - 0.5f), sharedBrush, 0.75f);
    }
    if (count < 2 || maxVal <= 0.0) return;

    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    if (w <= 2.0f || h <= 2.0f) return;

    const size_t slots = HistoryBuffer::kCapacity;
    const size_t offset = slots - count;
    const float slotStep = w / static_cast<float>(slots - 1);
    const float shiftX = (count > 2) ? (std::clamp(slideOffset, 0.0f, 1.0f) * slotStep) : 0.0f;

    D2D1_POINT_2F pts[HistoryBuffer::kCapacity]{};
    const size_t n = std::min(count, slots);
    for (size_t i = 0; i < n; ++i) {
        float x = rect.left + static_cast<float>(offset + i) * slotStep + shiftX;
        float norm = static_cast<float>(std::clamp(static_cast<double>(getVal(i)) / maxVal, 0.0, 1.0));
        float y = rect.bottom - norm * (h - 3.0f) - 1.5f;
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
        { 0.0f, D2D1::ColorF(strokeColor.r, strokeColor.g, strokeColor.b, 0.24f) },
        { 1.0f, D2D1::ColorF(strokeColor.r, strokeColor.g, strokeColor.b, 0.01f) },
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
        rt->DrawGeometry(lineGeo, sharedBrush, 1.55f, roundStroke);

        // Subtle live indicator dot at the right tip (smoothly enters through clip edge)
        const D2D1_POINT_2F& tip = pts[n - 1];
        D2D1_COLOR_F halo = strokeColor;
        halo.a = 0.28f;
        sharedBrush->SetColor(halo);
        rt->FillEllipse(D2D1::Ellipse(tip, 3.8f, 3.8f), sharedBrush);
        sharedBrush->SetColor(strokeColor);
        rt->FillEllipse(D2D1::Ellipse(tip, 2.0f, 2.0f), sharedBrush);
    }

    rt->PopAxisAlignedClip();
    lineGeo->Release();
    areaGeo->Release();
}

template <typename Getter>
void DrawMicroSparkBg(
    ID2D1RenderTarget* rt,
    ID2D1Factory* factory,
    ID2D1StrokeStyle* roundStroke,
    ID2D1SolidColorBrush* sharedBrush,
    const D2D1_RECT_F& rect,
    size_t count,
    Getter&& getVal,
    double minCeil,
    double hardMax,
    const D2D1_COLOR_F& baseColor,
    bool light,
    bool alreadyNormalized = false,
    float slideOffset = 0.0f,
    double yMaxOverride = 0.0) {
    constexpr size_t kWindow = 24;
    if (count < 2) return;

    size_t n = std::min(count, kWindow);
    size_t start = count - n;

    double yMax = 1.0;
    if (!alreadyNormalized) {
        if (yMaxOverride > 0.0) {
            yMax = std::clamp(yMaxOverride, minCeil, hardMax);
        } else {
            double peak = 0.0;
            for (size_t i = 0; i < n; ++i) {
                peak = std::max(peak, static_cast<double>(getVal(start + i)));
            }
            yMax = std::clamp(peak * 1.35, minCeil, hardMax);
        }
    }

    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    if (w <= 2.0f || h <= 2.0f) return;

    const float slotStep = w / static_cast<float>(kWindow - 1);
    const float shiftX = (n > 2) ? (std::clamp(slideOffset, 0.0f, 1.0f) * slotStep) : 0.0f;

    D2D1_POINT_2F pts[kWindow]{};
    const size_t offset = kWindow - n;
    for (size_t i = 0; i < n; ++i) {
        float x = rect.left + static_cast<float>(offset + i) * slotStep + shiftX;
        float norm = alreadyNormalized
            ? std::clamp(static_cast<float>(getVal(start + i)), 0.0f, 1.0f)
            : static_cast<float>(std::clamp(static_cast<double>(getVal(start + i)) / yMax, 0.0, 1.0));
        float y = rect.bottom - norm * (h - 2.5f) - 1.0f;
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

        const float minY = rect.top + 0.5f;
        const float maxY = rect.bottom - 0.5f;
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

    rt->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    D2D1_GRADIENT_STOP stops[2] = {
        { 0.0f, D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.14f) },
        { 1.0f, D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.00f) },
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
        D2D1_COLOR_F strokeCol = D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, light ? 0.36f : 0.30f);
        sharedBrush->SetColor(strokeCol);
        rt->DrawGeometry(lineGeo, sharedBrush, 1.1f, roundStroke);
    }

    rt->PopAxisAlignedClip();
    lineGeo->Release();
    areaGeo->Release();
}
