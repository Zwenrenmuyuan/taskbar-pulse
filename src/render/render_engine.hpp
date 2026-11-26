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
#include <d3d11_1.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wincodec.h>

#include "core/config.hpp"
#include "collectors/gold_price.hpp"
#include "collectors/sensors.hpp"
#include "render/animator.hpp"
#include <vector>

enum class MenuCheckStyle {
    None = 0,
    Check = 1,
    Radio = 2,
};

struct FluentMenuItem {
    UINT cmdId = 0;
    bool isSeparator = false;
    bool isDestructive = false;
    bool hasSubmenu = false;
    MenuCheckStyle checkStyle = MenuCheckStyle::None;
    bool checked = false;
    std::wstring label;
};

struct FluentMenuSpec {
    std::vector<FluentMenuItem> items;
    float cardWidthDip = 224.0f;
};

struct WidgetDimensions {
    float widthDip = 348.0f;
    float heightDip = 40.0f;
};

class RenderEngine {
public:
    RenderEngine();
    ~RenderEngine();

    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;

    bool Initialize();
    bool HasDComp() const { return m_dcompDevice != nullptr; }

    WidgetDimensions MeasureWidget(const AppConfig& cfg, float maxHeightDip = 40.0f) const;

    bool RenderWidgetToWindow(
        HWND hwnd,
        POINT ptScreen,
        UINT dpi,
        const AppConfig& cfg,
        const SystemSnapshot& snap,
        const HistoryBuffer& history,
        const GoldQuote& gold,
        const AnimatedMetrics& anim,
        float hoverProgress,
        float pressProgress);

    bool RenderFlyoutToWindow(
        HWND hwnd,
        POINT ptScreen,
        UINT dpi,
        const AppConfig& cfg,
        const SystemSnapshot& snap,
        const HistoryBuffer& history,
        const AnimatedMetrics& anim,
        const FlyoutAnimState& flyAnim,
        RECT outActionRectsPx[4]);

    static WidgetDimensions FlyoutDimensions() { return { 380.0f, 352.0f }; }
    static constexpr float MenuPadX() { return 8.0f; }
    static constexpr float MenuPadTop() { return 6.0f; }
    static constexpr float MenuPadBottom() { return 10.0f; }
    static constexpr float MenuRowHeightDip() { return 27.0f; }
    static WidgetDimensions MeasureMenu(const FluentMenuSpec& spec);
    static float MenuRowTopDip(const FluentMenuSpec& spec, int index);

    bool RenderMenuToWindow(
        HWND hwnd,
        POINT ptWindowScreen,
        UINT dpi,
        const AppConfig& cfg,
        const FluentMenuSpec& spec,
        int hoverIndex,
        int openSubIndex,
        const MenuAnimState& menuAnim,
        bool isSubmenuSurface,
        std::vector<RECT>& outItemRectsPx);

    bool AnimateWindowVisual(HWND hwnd, UINT dpi, const PopupMotionSpec& spec);
    bool ResetWindowVisual(HWND hwnd);
    void ResetWidgetTarget(HWND hwnd = nullptr);
    float MeasureMenuLabelWidth(const wchar_t* text) const;

    IWICBitmap* WidgetBitmap() const { return m_widgetTarget.bitmap; }
    IWICBitmap* FlyoutBitmap() const { return m_flyoutTarget.bitmap; }
    IWICBitmap* MenuBitmap() const { return m_menuTarget.bitmap; }
    IWICBitmap* SubmenuBitmap() const { return m_submenuTarget.bitmap; }

private:
    struct WicTarget {
        IWICBitmap* bitmap = nullptr;
        ID2D1RenderTarget* rt = nullptr;
        ID2D1SolidColorBrush* brMain = nullptr;
        ID2D1SolidColorBrush* brAux = nullptr;
        int width = 0;
        int height = 0;
    };

    struct DCompTargetEntry {
        HWND hwnd = nullptr;
        IDCompositionTarget* target = nullptr;
        IDCompositionVisual* visual = nullptr;
        IDCompositionSurface* surface = nullptr;
        IDCompositionEffectGroup* effectGroup = nullptr;
        IDCompositionTranslateTransform* translate = nullptr;
        IDCompositionScaleTransform* scale = nullptr;
        IDCompositionTransform* transformGroup = nullptr;
        int width = 0;
        int height = 0;
    };

    bool EnsureWicTarget(WicTarget& target, int widthPx, int heightPx, UINT dpi);
    void ReleaseWicTarget(WicTarget& target);
    bool EnsureDCompDevice();
    void ResetDCompDevice();
    void ReleaseDCompTargetEntry(DCompTargetEntry& entry);
    DCompTargetEntry* FindDCompEntry(HWND hwnd);
    DCompTargetEntry* EnsureDCompSurface(HWND hwnd, int widthPx, int heightPx);
    bool UsesDCompWindow(HWND hwnd) const;
    void SyncPopupWindowBounds(HWND hwnd, POINT ptScreen, int widthPx, int heightPx);

    template <typename DrawFn>
    bool RenderToDCompSurface(HWND hwnd, int widthPx, int heightPx, UINT dpi, DrawFn&& drawFn);

    void DrawWidgetContent(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brMain,
        ID2D1SolidColorBrush* brAux,
        const WidgetDimensions& dim,
        const AppConfig& cfg,
        const SystemSnapshot& snap,
        const HistoryBuffer& history,
        const GoldQuote& gold,
        const AnimatedMetrics& anim,
        float hoverProgress,
        float pressProgress);

    void DrawFlyoutContent(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brMain,
        ID2D1SolidColorBrush* brAux,
        UINT dpi,
        const AppConfig& cfg,
        const SystemSnapshot& snap,
        const HistoryBuffer& history,
        const AnimatedMetrics& anim,
        const FlyoutAnimState& flyAnim,
        RECT outActionRectsPx[4]);

    void DrawMenuContent(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brMain,
        ID2D1SolidColorBrush* brAux,
        UINT dpi,
        const AppConfig& cfg,
        const FluentMenuSpec& spec,
        int hoverIndex,
        int openSubIndex,
        const MenuAnimState& menuAnim,
        std::vector<RECT>& outItemRectsPx);

    void DrawTextFormatted(
        ID2D1RenderTarget* rt,
        const wchar_t* text,
        IDWriteTextFormat* fmt,
        const D2D1_RECT_F& rect,
        ID2D1Brush* brush,
        bool tabularFigures);
    float MeasureTextWidth(
        const wchar_t* text,
        IDWriteTextFormat* fmt,
        bool tabularFigures) const;

    ID2D1Factory1* m_d2dFactory = nullptr;
    IDWriteFactory* m_dwFactory = nullptr;
    IWICImagingFactory* m_wicFactory = nullptr;
    IDWriteTypography* m_tabularTypo = nullptr;
    ID2D1StrokeStyle* m_roundStroke = nullptr;

    IDWriteTextFormat* m_fmtLabel = nullptr;
    IDWriteTextFormat* m_fmtValue = nullptr;
    IDWriteTextFormat* m_fmtMinLabel = nullptr;
    IDWriteTextFormat* m_fmtMinValue = nullptr;
    IDWriteTextFormat* m_fmtMinSub = nullptr;
    IDWriteTextFormat* m_fmtMenuText = nullptr;
    IDWriteTextFormat* m_fmtFlyTitle = nullptr;
    IDWriteTextFormat* m_fmtFlySub = nullptr;
    IDWriteTextFormat* m_fmtFlyCardLabel = nullptr;
    IDWriteTextFormat* m_fmtFlyCardBig = nullptr;
    IDWriteTextFormat* m_fmtFlyBtn = nullptr;

    ID3D11Device* m_d3dDevice = nullptr;
    ID2D1Device* m_d2dDevice = nullptr;
    ID2D1DeviceContext* m_d2dDc = nullptr;
    ID2D1SolidColorBrush* m_dcompBrMain = nullptr;
    ID2D1SolidColorBrush* m_dcompBrAux = nullptr;
    IDCompositionDevice* m_dcompDevice = nullptr;
    std::vector<DCompTargetEntry> m_dcompTargets;

    WicTarget m_widgetTarget{};
    WicTarget m_flyoutTarget{};
    WicTarget m_menuTarget{};
    WicTarget m_submenuTarget{};
};

template <typename DrawFn>
bool RenderEngine::RenderToDCompSurface(HWND hwnd, int widthPx, int heightPx, UINT dpi, DrawFn&& drawFn) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        DCompTargetEntry* entry = EnsureDCompSurface(hwnd, widthPx, heightPx);
        if (!entry || !entry->surface || !m_dcompBrMain || !m_dcompBrAux) {
            ResetDCompDevice();
            continue;
        }

        IDXGISurface* dxgiSurf = nullptr;
        POINT offset{};
        HRESULT hr = entry->surface->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurf), &offset);
        if (FAILED(hr) || !dxgiSurf) {
            if (dxgiSurf) dxgiSurf->Release();
            ResetDCompDevice();
            continue;
        }

        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<FLOAT>(dpi), static_cast<FLOAT>(dpi));
        ID2D1Bitmap1* d2dBmp = nullptr;
        hr = m_d2dDc->CreateBitmapFromDxgiSurface(dxgiSurf, &bp, &d2dBmp);
        if (FAILED(hr) || !d2dBmp) {
            if (d2dBmp) d2dBmp->Release();
            dxgiSurf->Release();
            entry->surface->EndDraw();
            ResetDCompDevice();
            continue;
        }

        m_d2dDc->SetTarget(d2dBmp);
        m_d2dDc->SetDpi(static_cast<FLOAT>(dpi), static_cast<FLOAT>(dpi));
        m_d2dDc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        float scale = static_cast<float>(dpi) / 96.0f;
        float offX = static_cast<float>(offset.x) / scale;
        float offY = static_cast<float>(offset.y) / scale;

        m_d2dDc->BeginDraw();
        m_d2dDc->SetTransform(D2D1::Matrix3x2F::Translation(offX, offY));
        m_d2dDc->PushAxisAlignedClip(
            D2D1::RectF(0.0f, 0.0f, static_cast<float>(widthPx) / scale, static_cast<float>(heightPx) / scale),
            D2D1_ANTIALIAS_MODE_ALIASED);
        m_d2dDc->Clear(D2D1::ColorF(0, 0, 0, 0));

        drawFn(m_d2dDc, m_dcompBrMain, m_dcompBrAux);

        m_d2dDc->PopAxisAlignedClip();
        m_d2dDc->SetTransform(D2D1::Matrix3x2F::Identity());
        HRESULT hrEnd = m_d2dDc->EndDraw();
        m_d2dDc->SetTarget(nullptr);

        d2dBmp->Release();
        dxgiSurf->Release();

        HRESULT hrSurfEnd = entry->surface->EndDraw();
        HRESULT hrCommit = m_dcompDevice->Commit();

        if (SUCCEEDED(hrEnd) && SUCCEEDED(hrSurfEnd) && SUCCEEDED(hrCommit)) {
            return true;
        }
        ResetDCompDevice();
    }
    return false;
}
