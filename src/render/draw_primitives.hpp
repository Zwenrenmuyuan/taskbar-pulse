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

void DrawVectorArrow(ID2D1RenderTarget* rt, ID2D1Factory* factory, float cx, float cy, bool up, ID2D1Brush* brush);
void DrawMiniBar(ID2D1RenderTarget* rt, float x, float y, float w, float h, double pct, ID2D1Brush* trackBrush, ID2D1Brush* fillBrush);
bool PresentWicToWindow(HWND hwnd, POINT ptScreen, IWICBitmap* wicBmp, int wPx, int hPx);
void DrawCardShadow(ID2D1RenderTarget* rt, float padX, float padTop, float innerW, float innerH, float cornerR, bool light);
ID2D1StrokeStyle* CreateRoundStrokeStyle(ID2D1Factory* factory);
void DrawMenuCheckIcon(ID2D1RenderTarget* rt, ID2D1StrokeStyle* style, float cx, float cy, ID2D1Brush* brush);
void DrawMenuChevronRight(ID2D1RenderTarget* rt, ID2D1StrokeStyle* style, float cx, float cy, ID2D1Brush* brush);

template <typename Getter>
void DrawSparkline(
    ID2D1RenderTarget* rt,
    ID2D1Factory* factory,
    const D2D1_RECT_F& rect,
    size_t count,
    Getter&& getVal,
    double maxVal,
    const D2D1_COLOR_F& strokeColor,
    bool drawGrid = true) {
    if (drawGrid) {
        ID2D1SolidColorBrush* gridBrush = nullptr;
        if (SUCCEEDED(rt->CreateSolidColorBrush(Rgba(128, 138, 152, 0.14f), &gridBrush))) {
            float midY = (rect.top + rect.bottom) * 0.5f;
            rt->DrawLine(D2D1::Point2F(rect.left, midY), D2D1::Point2F(rect.right, midY), gridBrush, 0.75f);
            rt->DrawLine(D2D1::Point2F(rect.left, rect.bottom - 0.5f), D2D1::Point2F(rect.right, rect.bottom - 0.5f), gridBrush, 0.75f);
            gridBrush->Release();
        }
    }
    if (count < 2 || maxVal <= 0.0) return;

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
        float w = rect.right - rect.left;
        float h = rect.bottom - rect.top;
        size_t slots = HistoryBuffer::kCapacity;
        size_t offset = slots - count;

        auto pointAt = [&](size_t i) -> D2D1_POINT_2F {
            float x = rect.left + (static_cast<float>(offset + i) / static_cast<float>(slots - 1)) * w;
            float norm = static_cast<float>(std::clamp(static_cast<double>(getVal(i)) / maxVal, 0.0, 1.0));
            float y = rect.bottom - norm * (h - 2.0f) - 1.0f;
            return D2D1::Point2F(x, y);
        };

        D2D1_POINT_2F p0 = pointAt(0);
        lineSink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
        areaSink->BeginFigure(D2D1::Point2F(p0.x, rect.bottom), D2D1_FIGURE_BEGIN_FILLED);
        areaSink->AddLine(p0);

        for (size_t i = 1; i < count; ++i) {
            D2D1_POINT_2F p = pointAt(i);
            lineSink->AddLine(p);
            areaSink->AddLine(p);
        }
        D2D1_POINT_2F pLast = pointAt(count - 1);
        areaSink->AddLine(D2D1::Point2F(pLast.x, rect.bottom));

        lineSink->EndFigure(D2D1_FIGURE_END_OPEN);
        areaSink->EndFigure(D2D1_FIGURE_END_CLOSED);
        lineSink->Close();
        areaSink->Close();
    }
    SafeRelease(lineSink);
    SafeRelease(areaSink);

    D2D1_COLOR_F fillCol = strokeColor;
    fillCol.a = 0.16f;
    ID2D1SolidColorBrush* fillBrush = nullptr;
    ID2D1SolidColorBrush* lineBrush = nullptr;
    if (SUCCEEDED(rt->CreateSolidColorBrush(fillCol, &fillBrush))) {
        rt->FillGeometry(areaGeo, fillBrush);
        fillBrush->Release();
    }
    if (SUCCEEDED(rt->CreateSolidColorBrush(strokeColor, &lineBrush))) {
        rt->DrawGeometry(lineGeo, lineBrush, 1.5f);
        lineBrush->Release();
    }

    lineGeo->Release();
    areaGeo->Release();
}

template <typename Getter>
void DrawMicroSparkBg(
    ID2D1RenderTarget* rt,
    ID2D1Factory* factory,
    const D2D1_RECT_F& rect,
    size_t count,
    Getter&& getVal,
    double minCeil,
    double hardMax,
    const D2D1_COLOR_F& baseColor,
    bool light,
    bool alreadyNormalized = false) {
    constexpr size_t kWindow = 24;
    if (count < 2) return;

    size_t n = std::min(count, kWindow);
    size_t start = count - n;

    double yMax = 1.0;
    if (!alreadyNormalized) {
        double peak = 0.0;
        for (size_t i = 0; i < n; ++i) {
            peak = std::max(peak, static_cast<double>(getVal(start + i)));
        }
        yMax = std::clamp(peak * 1.35, minCeil, hardMax);
    }

    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    if (w <= 2.0f || h <= 2.0f) return;

    D2D1_POINT_2F pts[kWindow]{};
    const size_t offset = kWindow - n;
    for (size_t i = 0; i < n; ++i) {
        float x = rect.left + (static_cast<float>(offset + i) / static_cast<float>(kWindow - 1)) * w;
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
        { 0.0f, D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, light ? 0.14f : 0.14f) },
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

    D2D1_COLOR_F strokeCol = D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, light ? 0.36f : 0.30f);
    ID2D1SolidColorBrush* lineBrush = nullptr;
    ID2D1StrokeStyle* strokeStyle = CreateRoundStrokeStyle(factory);
    if (SUCCEEDED(rt->CreateSolidColorBrush(strokeCol, &lineBrush)) && lineBrush) {
        rt->DrawGeometry(lineGeo, lineBrush, 1.1f, strokeStyle);
        lineBrush->Release();
    }
    SafeRelease(strokeStyle);

    rt->PopAxisAlignedClip();
    lineGeo->Release();
    areaGeo->Release();
}
