#include "render/render_engine.hpp"
#include "render/palette.hpp"
#include "render/draw_primitives.hpp"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kMenuRowH = RenderEngine::MenuRowHeightDip();
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

float RenderEngine::MenuRowTopDip(const FluentMenuSpec& spec, int index) {
    if (index < 0 || index >= static_cast<int>(spec.items.size())) return kMenuInnerPadY;
    float curY = kMenuInnerPadY;
    for (int i = 0; i < index; ++i) {
        curY += spec.items[static_cast<size_t>(i)].isSeparator ? kMenuSepH : kMenuRowH;
    }
    return curY;
}

void RenderEngine::DrawMenuContent(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brMain,
    ID2D1SolidColorBrush* brAux,
    UINT dpi,
    const AppConfig& cfg,
    const FluentMenuSpec& spec,
    int hoverIndex,
    int openSubIndex,
    const MenuAnimState& menuAnim,
    std::vector<RECT>& outItemRectsPx) {
    const WidgetDimensions dim = MeasureMenu(spec);
    const float scale = static_cast<float>(dpi) / 96.0f;
    const bool light = ResolveEffectiveLightTheme(cfg);
    const Palette pal = MakePalette(light);
    const float padX = MenuPadX();
    const float padTop = MenuPadTop();
    const float innerW = spec.cardWidthDip;
    const float innerH = dim.heightDip - padTop - MenuPadBottom();

    DrawCardShadow(rt, brMain, padX, padTop, innerW, innerH, 10.0f, light);

    D2D1_MATRIX_3X2_F prevTransform{};
    rt->GetTransform(&prevTransform);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(padX, padTop) * prevTransform);

    const D2D1_COLOR_F hoverCol = light ? Rgba(20, 20, 24, 0.056f) : Rgba(255, 255, 255, 0.088f);
    const D2D1_COLOR_F activeSubCol = light ? Rgba(20, 20, 24, 0.036f) : Rgba(255, 255, 255, 0.050f);
    const D2D1_COLOR_F dangerHoverCol = light ? pal.badgeHotBg : pal.badgeHotBg;
    const D2D1_COLOR_F checkCol = pal.accentCpu;

    D2D1_ROUNDED_RECT outer = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, innerW - 0.5f, innerH - 0.5f), 11.0f, 11.0f);
    brMain->SetColor(pal.flyBg);
    rt->FillRoundedRectangle(&outer, brMain);
    brMain->SetColor(pal.flyBorder);
    rt->DrawRoundedRectangle(&outer, brMain, 1.0f);

    // Subtle top bevel highlight
    brMain->SetColor(pal.flyHighlight);
    rt->DrawLine(D2D1::Point2F(12.0f, 1.5f), D2D1::Point2F(innerW - 12.0f, 1.5f), brMain, 1.0f);

    // First draw static highlight under an open submenu's parent row if pointer is elsewhere
    if (openSubIndex >= 0 && openSubIndex < static_cast<int>(spec.items.size()) &&
        !spec.items[static_cast<size_t>(openSubIndex)].isSeparator) {
        float subY = MenuRowTopDip(spec, openSubIndex);
        D2D1_ROUNDED_RECT subPill = D2D1::RoundedRect(
            D2D1::RectF(5.0f, subY + 1.5f, innerW - 5.0f, subY + kMenuRowH - 1.5f), 5.5f, 5.5f);
        brMain->SetColor(activeSubCol);
        rt->FillRoundedRectangle(&subPill, brMain);
    }

    // Smoothly gliding active hover pill
    if (menuAnim.hasPillPos && menuAnim.pillAlpha > 0.005f) {
        D2D1_COLOR_F pillCol = anim::LerpColor(hoverCol, dangerHoverCol, std::clamp(menuAnim.pillDanger, 0.0f, 1.0f));
        pillCol.a *= std::clamp(menuAnim.pillAlpha, 0.0f, 1.0f);
        D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(
            D2D1::RectF(5.0f, menuAnim.pillY + 1.5f, innerW - 5.0f, menuAnim.pillY + kMenuRowH - 1.5f), 5.5f, 5.5f);
        brMain->SetColor(pillCol);
        rt->FillRoundedRectangle(&pill, brMain);
    }

    outItemRectsPx.assign(spec.items.size(), RECT{});
    float curY = kMenuInnerPadY;

    for (size_t i = 0; i < spec.items.size(); ++i) {
        const auto& it = spec.items[i];
        if (it.isSeparator) {
            float sepY = curY + kMenuSepH * 0.5f;
            brMain->SetColor(pal.divider);
            rt->DrawLine(D2D1::Point2F(12.0f, sepY), D2D1::Point2F(innerW - 12.0f, sepY), brMain, 1.0f);
            curY += kMenuSepH;
            continue;
        }

        const bool hovered = (static_cast<int>(i) == hoverIndex);
        const bool subOpen = (static_cast<int>(i) == openSubIndex);

        float midY = curY + kMenuRowH * 0.5f;
        if (it.checked) {
            brMain->SetColor(checkCol);
            if (it.checkStyle == MenuCheckStyle::Radio) {
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(17.5f, midY), 2.6f, 2.6f), brMain);
            } else if (it.checkStyle == MenuCheckStyle::Check) {
                DrawMenuCheckIcon(rt, m_roundStroke, 17.5f, midY, brMain);
            }
        }

        float textRight = innerW - (it.hasSubmenu ? 24.0f : 12.0f);
        D2D1_COLOR_F txtCol = pal.labelStrong;
        if (it.isDestructive) {
            float dangerBlend = hovered ? std::clamp(menuAnim.pillAlpha, 0.0f, 1.0f) : 0.0f;
            txtCol = anim::LerpColor(pal.labelStrong, pal.hot, dangerBlend);
        }
        brAux->SetColor(txtCol);
        DrawTextFormatted(rt, it.label.c_str(), m_fmtMenuText,
            D2D1::RectF(30.0f, curY, textRight, curY + kMenuRowH), brAux, false);

        if (it.hasSubmenu) {
            brMain->SetColor((hovered || subOpen) ? pal.labelStrong : pal.label);
            DrawMenuChevronRight(rt, m_roundStroke, innerW - 15.0f, midY, brMain);
        }

        outItemRectsPx[i] = RECT{
            static_cast<LONG>(std::lround((padX + 4.0f) * scale)),
            static_cast<LONG>(std::lround((padTop + curY) * scale)),
            static_cast<LONG>(std::lround((padX + innerW - 4.0f) * scale)),
            static_cast<LONG>(std::lround((padTop + curY + kMenuRowH) * scale)),
        };
        curY += kMenuRowH;
    }

    rt->SetTransform(prevTransform);
}

bool RenderEngine::RenderMenuToWindow(
    HWND hwnd,
    POINT ptWindowScreen,
    UINT dpi,
    const AppConfig& cfg,
    const FluentMenuSpec& spec,
    int hoverIndex,
    int openSubIndex,
    const MenuAnimState& menuAnim,
    bool isSubmenuSurface,
    std::vector<RECT>& outItemRectsPx) {
    const WidgetDimensions dim = MeasureMenu(spec);
    float scale = static_cast<float>(dpi) / 96.0f;
    int wPx = std::max(1, static_cast<int>(std::lround(dim.widthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(dim.heightDip * scale)));

    if (UsesDCompWindow(hwnd)) {
        SyncPopupWindowBounds(hwnd, ptWindowScreen, wPx, hPx);
        return RenderToDCompSurface(hwnd, wPx, hPx, dpi, [&](ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brMain, ID2D1SolidColorBrush* brAux) {
            DrawMenuContent(rt, brMain, brAux, dpi, cfg, spec, hoverIndex, openSubIndex, menuAnim, outItemRectsPx);
        });
    }

    WicTarget& target = isSubmenuSurface ? m_submenuTarget : m_menuTarget;
    if (!EnsureWicTarget(target, wPx, hPx, dpi)) return false;

    ID2D1RenderTarget* rt = target.rt;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawMenuContent(rt, target.brMain, target.brAux, dpi, cfg, spec, hoverIndex, openSubIndex, menuAnim, outItemRectsPx);
    if (FAILED(rt->EndDraw())) return false;
    return hwnd ? PresentWicToWindow(hwnd, ptWindowScreen, target.bitmap, wPx, hPx) : true;
}
