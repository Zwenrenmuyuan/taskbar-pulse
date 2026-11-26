#include "render/draw_primitives.hpp"

void DrawVectorArrow(ID2D1RenderTarget* rt, ID2D1StrokeStyle* roundStroke, float cx, float cy, bool up, ID2D1Brush* brush) {
    float halfH = 3.6f;
    float headW = 2.6f;
    float headH = 2.5f;
    if (up) {
        rt->DrawLine(D2D1::Point2F(cx, cy + halfH), D2D1::Point2F(cx, cy - halfH), brush, 1.35f, roundStroke);
        rt->DrawLine(D2D1::Point2F(cx - headW, cy - halfH + headH), D2D1::Point2F(cx, cy - halfH), brush, 1.35f, roundStroke);
        rt->DrawLine(D2D1::Point2F(cx + headW, cy - halfH + headH), D2D1::Point2F(cx, cy - halfH), brush, 1.35f, roundStroke);
    } else {
        rt->DrawLine(D2D1::Point2F(cx, cy - halfH), D2D1::Point2F(cx, cy + halfH), brush, 1.35f, roundStroke);
        rt->DrawLine(D2D1::Point2F(cx - headW, cy + halfH - headH), D2D1::Point2F(cx, cy + halfH), brush, 1.35f, roundStroke);
        rt->DrawLine(D2D1::Point2F(cx + headW, cy + halfH - headH), D2D1::Point2F(cx, cy + halfH), brush, 1.35f, roundStroke);
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
    if (clamped > 0.01f) {
        float fillW = std::max(h, w * clamped);
        D2D1_ROUNDED_RECT fill = D2D1::RoundedRect(D2D1::RectF(x, y, x + fillW, y + h), r, r);
        rt->FillRoundedRectangle(&fill, fillBrush);
    }
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
    for (int s = 5; s >= 1; --s) {
        float ex = static_cast<float>(s);
        float alpha = (light ? 0.042f : 0.11f) * (1.0f - static_cast<float>(s - 1) / 5.5f);
        brShadow->SetColor(Rgba(0, 0, 0, alpha));
        D2D1_ROUNDED_RECT sr = D2D1::RoundedRect(
            D2D1::RectF(padX - ex + 0.5f, padTop - ex + 1.5f, padX + innerW + ex - 0.5f, padTop + innerH + ex + 1.5f),
            cornerR + ex * 0.5f, cornerR + ex * 0.5f);
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
