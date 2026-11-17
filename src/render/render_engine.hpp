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

    WidgetDimensions MeasureWidget(const AppConfig& cfg, float maxHeightDip = 40.0f) const;

    bool RenderWidgetToWindow(
        HWND hwnd,
        POINT ptScreen,
        UINT dpi,
        const AppConfig& cfg,
        const SystemSnapshot& snap,
        const HistoryBuffer& history,
        const GoldQuote& gold,
        bool isHovered,
        bool isPressed);

    bool RenderFlyoutToWindow(
        HWND hwnd,
        POINT ptScreen,
        UINT dpi,
        const AppConfig& cfg,
        const SystemSnapshot& snap,
        const HistoryBuffer& history,
        int hoverActionIndex,
        RECT outActionRectsPx[4]);

    static WidgetDimensions FlyoutDimensions() { return { 380.0f, 352.0f }; }
    static constexpr float MenuPadX() { return 8.0f; }
    static constexpr float MenuPadTop() { return 6.0f; }
    static constexpr float MenuPadBottom() { return 10.0f; }
    static WidgetDimensions MeasureMenu(const FluentMenuSpec& spec);

    bool RenderMenuToWindow(
        HWND hwnd,
        POINT ptWindowScreen,
        UINT dpi,
        const AppConfig& cfg,
        const FluentMenuSpec& spec,
        int hoverIndex,
        int openSubIndex,
        bool isSubmenuSurface,
        std::vector<RECT>& outItemRectsPx);

    void ResetWidgetTarget();

    IWICBitmap* WidgetBitmap() const { return m_widgetTarget.bitmap; }
    IWICBitmap* FlyoutBitmap() const { return m_flyoutTarget.bitmap; }
    IWICBitmap* MenuBitmap() const { return m_menuTarget.bitmap; }
    IWICBitmap* SubmenuBitmap() const { return m_submenuTarget.bitmap; }

private:
    struct WicTarget {
        IWICBitmap* bitmap = nullptr;
        ID2D1RenderTarget* rt = nullptr;
        int width = 0;
        int height = 0;
    };

    bool EnsureWicTarget(WicTarget& target, int widthPx, int heightPx, UINT dpi);
    void ReleaseWicTarget(WicTarget& target);
    bool EnsureDCompDevice();
    void ResetDCompDevice();
    bool EnsureDCompSurface(HWND hwnd, int widthPx, int heightPx);
    bool RenderWidgetViaDComp(
        HWND hwnd,
        int widthPx,
        int heightPx,
        UINT dpi,
        const WidgetDimensions& dim,
        const AppConfig& cfg,
        const SystemSnapshot& snap,
        const HistoryBuffer& history,
        const GoldQuote& gold,
        bool isHovered,
        bool isPressed);
    void DrawWidgetContent(
        ID2D1RenderTarget* rt,
        const WidgetDimensions& dim,
        const AppConfig& cfg,
        const SystemSnapshot& snap,
        const HistoryBuffer& history,
        const GoldQuote& gold,
        bool isHovered,
        bool isPressed);
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
    IDCompositionDevice* m_dcompDevice = nullptr;
    HWND m_dcompHwnd = nullptr;
    IDCompositionTarget* m_dcompTarget = nullptr;
    IDCompositionVisual* m_dcompVisual = nullptr;
    IDCompositionSurface* m_dcompSurface = nullptr;
    int m_dcompW = 0;
    int m_dcompH = 0;

    WicTarget m_widgetTarget{};
    WicTarget m_flyoutTarget{};
    WicTarget m_menuTarget{};
    WicTarget m_submenuTarget{};
};
