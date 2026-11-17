#include "render/render_engine.hpp"
#include "render/palette.hpp"
#include "render/draw_primitives.hpp"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMenuRowH = 27.0f;
constexpr float kMenuSepH = 9.0f;
constexpr float kMenuInnerPadY = 6.0f;
} // namespace

WidgetDimensions RenderEngine::MeasureMenu(const FluentMenuSpec& spec) {
    float innerH = kMenuInnerPadY * 2.0f;
    for (const auto& it : spec.items) {
        innerH += it.isSeparator ? kMenuSepH : kMenuRowH;
    }
    return { spec.cardWidthDip + MenuPadX() * 2.0f, innerH + MenuPadTop() + MenuPadBottom() };
}

bool RenderEngine::RenderMenuToWindow(
    HWND hwnd,
    POINT ptWindowScreen,
    UINT dpi,
    const AppConfig& cfg,
    const FluentMenuSpec& spec,
    int hoverIndex,
    int openSubIndex,
    bool isSubmenuSurface,
    std::vector<RECT>& outItemRectsPx) {
    const WidgetDimensions dim = MeasureMenu(spec);
    float scale = static_cast<float>(dpi) / 96.0f;
    int wPx = std::max(1, static_cast<int>(std::lround(dim.widthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(dim.heightDip * scale)));

    WicTarget& target = isSubmenuSurface ? m_submenuTarget : m_menuTarget;
    if (!EnsureWicTarget(target, wPx, hPx, dpi)) return false;

    const bool light = ResolveEffectiveLightTheme(cfg);
    const Palette pal = MakePalette(light);
    const float padX = MenuPadX();
    const float padTop = MenuPadTop();
    const float innerW = spec.cardWidthDip;
    const float innerH = dim.heightDip - padTop - MenuPadBottom();

    ID2D1RenderTarget* rt = target.rt;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));

    DrawCardShadow(rt, padX, padTop, innerW, innerH, 10.0f, light);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(padX, padTop));

    ID2D1SolidColorBrush* brBg = nullptr;
    ID2D1SolidColorBrush* brBorder = nullptr;
    ID2D1SolidColorBrush* brHover = nullptr;
    ID2D1SolidColorBrush* brActiveSub = nullptr;
    ID2D1SolidColorBrush* brDangerHover = nullptr;
    ID2D1SolidColorBrush* brText = nullptr;
    ID2D1SolidColorBrush* brSubText = nullptr;
    ID2D1SolidColorBrush* brCheck = nullptr;
    ID2D1SolidColorBrush* brSep = nullptr;
    ID2D1SolidColorBrush* brDanger = nullptr;
    ID2D1StrokeStyle* roundStroke = CreateRoundStrokeStyle(m_d2dFactory);

    rt->CreateSolidColorBrush(pal.flyBg, &brBg);
    rt->CreateSolidColorBrush(pal.flyBorder, &brBorder);
    rt->CreateSolidColorBrush(light ? Rgba(15, 23, 42, 0.055f) : Rgba(255, 255, 255, 0.085f), &brHover);
    rt->CreateSolidColorBrush(light ? Rgba(15, 23, 42, 0.04f) : Rgba(255, 255, 255, 0.055f), &brActiveSub);
    rt->CreateSolidColorBrush(light ? Rgba(185, 28, 28, 0.08f) : Rgba(248, 113, 113, 0.14f), &brDangerHover);
    rt->CreateSolidColorBrush(pal.labelStrong, &brText);
    rt->CreateSolidColorBrush(pal.label, &brSubText);
    rt->CreateSolidColorBrush(light ? Rgba(15, 118, 110, 1.0f) : Rgba(110, 231, 192, 1.0f), &brCheck);
    rt->CreateSolidColorBrush(pal.divider, &brSep);
    rt->CreateSolidColorBrush(pal.hot, &brDanger);

    D2D1_ROUNDED_RECT outer = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, innerW - 0.5f, innerH - 0.5f), 10.0f, 10.0f);
    rt->FillRoundedRectangle(&outer, brBg);
    rt->DrawRoundedRectangle(&outer, brBorder, 1.0f);

    outItemRectsPx.assign(spec.items.size(), RECT{});
    float curY = kMenuInnerPadY;

    for (size_t i = 0; i < spec.items.size(); ++i) {
        const auto& it = spec.items[i];
        if (it.isSeparator) {
            float sepY = curY + kMenuSepH * 0.5f;
            rt->DrawLine(D2D1::Point2F(12.0f, sepY), D2D1::Point2F(innerW - 12.0f, sepY), brSep, 1.0f);
            curY += kMenuSepH;
            continue;
        }

        const bool hovered = (static_cast<int>(i) == hoverIndex);
        const bool subOpen = (static_cast<int>(i) == openSubIndex);
        if (hovered || subOpen) {
            D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(
                D2D1::RectF(5.0f, curY + 1.5f, innerW - 5.0f, curY + kMenuRowH - 1.5f), 5.5f, 5.5f);
            ID2D1SolidColorBrush* pillBrush = (hovered && it.isDestructive) ? brDangerHover : (hovered ? brHover : brActiveSub);
            rt->FillRoundedRectangle(&pill, pillBrush);
        }

        float midY = curY + kMenuRowH * 0.5f;
        if (it.checked) {
            if (it.checkStyle == MenuCheckStyle::Radio) {
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(17.5f, midY), 2.6f, 2.6f), brCheck);
            } else if (it.checkStyle == MenuCheckStyle::Check) {
                DrawMenuCheckIcon(rt, roundStroke, 17.5f, midY, brCheck);
            }
        }

        float textRight = innerW - (it.hasSubmenu ? 24.0f : 12.0f);
        ID2D1SolidColorBrush* txtBrush = (hovered && it.isDestructive) ? brDanger : brText;
        DrawTextFormatted(rt, it.label.c_str(), m_fmtMenuText,
            D2D1::RectF(30.0f, curY, textRight, curY + kMenuRowH), txtBrush, false);

        if (it.hasSubmenu) {
            DrawMenuChevronRight(rt, roundStroke, innerW - 15.0f, midY, (hovered || subOpen) ? brText : brSubText);
        }

        outItemRectsPx[i] = RECT{
            static_cast<LONG>(std::lround((padX + 4.0f) * scale)),
            static_cast<LONG>(std::lround((padTop + curY) * scale)),
            static_cast<LONG>(std::lround((padX + innerW - 4.0f) * scale)),
            static_cast<LONG>(std::lround((padTop + curY + kMenuRowH) * scale)),
        };
        curY += kMenuRowH;
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());

    SafeRelease(roundStroke);
    SafeRelease(brBg);
    SafeRelease(brBorder);
    SafeRelease(brHover);
    SafeRelease(brActiveSub);
    SafeRelease(brDangerHover);
    SafeRelease(brText);
    SafeRelease(brSubText);
    SafeRelease(brCheck);
    SafeRelease(brSep);
    SafeRelease(brDanger);

    if (FAILED(rt->EndDraw())) return false;
    return hwnd ? PresentWicToWindow(hwnd, ptWindowScreen, target.bitmap, wPx, hPx) : true;
}
