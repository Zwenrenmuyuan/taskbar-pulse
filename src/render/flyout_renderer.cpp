#include "render/render_engine.hpp"
#include "render/palette.hpp"
#include "render/draw_primitives.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

void RenderEngine::DrawFlyoutContent(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brMain,
    ID2D1SolidColorBrush* brAux,
    UINT dpi,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    const AnimatedMetrics& anim,
    const FlyoutAnimState& flyAnim,
    RECT outActionRectsPx[4]) {
    const float scale = static_cast<float>(dpi) / 96.0f;
    const bool light = ResolveEffectiveLightTheme(cfg);
    const Palette pal = MakePalette(light);
    const bool useAnimColors = anim.colorsInitialized && anim.lastLightTheme == light && !anim.lastMinimalStyle;

    // Soft layered drop shadow around inner 368x340 card
    constexpr float padLeft = 6.0f;
    constexpr float padTop = 5.0f;
    constexpr float innerW = 368.0f;
    constexpr float innerH = 340.0f;

    DrawCardShadow(rt, brMain, padLeft, padTop, innerW, innerH, 12.0f, light);

    D2D1_MATRIX_3X2_F prevTransform{};
    rt->GetTransform(&prevTransform);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(padLeft, padTop) * prevTransform);

    // Outer rounded panel
    D2D1_ROUNDED_RECT outer = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, innerW - 0.5f, innerH - 0.5f), 12.0f, 12.0f);
    brMain->SetColor(pal.flyBg);
    rt->FillRoundedRectangle(&outer, brMain);
    brMain->SetColor(pal.flyBorder);
    rt->DrawRoundedRectangle(&outer, brMain, 1.0f);

    // Subtle top bevel highlight
    brMain->SetColor(light ? Rgba(255, 255, 255, 0.65f) : Rgba(255, 255, 255, 0.08f));
    rt->DrawLine(D2D1::Point2F(14.0f, 1.5f), D2D1::Point2F(innerW - 14.0f, 1.5f), brMain, 1.0f);

    // Header
    brMain->SetColor(pal.labelStrong);
    DrawTextFormatted(rt, L"系统性能概览", m_fmtFlyTitle, D2D1::RectF(16.0f, 12.0f, 160.0f, 34.0f), brMain, false);
    const wchar_t* gpuTitle = snap.gpuName.empty() ? L"集成/默认显卡" : snap.gpuName.c_str();
    brAux->SetColor(pal.label);
    m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    DrawTextFormatted(rt, gpuTitle, m_fmtFlySub, D2D1::RectF(130.0f, 14.0f, innerW - 16.0f, 32.0f), brAux, false);
    m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 2x2 Cards
    float padX = 14.0f;
    float gap = 10.0f;
    float cardW = (innerW - padX * 2.0f - gap) * 0.5f;
    float cardH = 112.0f;
    float topY = 44.0f;

    auto drawCardBox = [&](float x, float y, float w, float h) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 9.0f, 9.0f);
        brMain->SetColor(pal.cardBg);
        rt->FillRoundedRectangle(&rr, brMain);
        brMain->SetColor(pal.cardBorder);
        rt->DrawRoundedRectangle(&rr, brMain, 1.0f);
    };

    const double dispVramPct = anim.initialized ? anim.vramPct : (snap.hasVram ? snap.vramPercent : 0.0);
    const double dispRamPct = anim.initialized ? anim.ramPct : snap.ramPercent;
    const float slideOffset = anim.historySlide;

    wchar_t buf[64]{};

    // Card 1: CPU
    {
        float cx = padX, cy = topY;
        drawCardBox(cx, cy, cardW, cardH);
        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, L"处理器 CPU", m_fmtFlyCardLabel, D2D1::RectF(cx + 12, cy + 8, cx + cardW - 12, cy + 24), brAux, false);

        if (snap.hasCpuTemp) {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f°C", snap.cpuTempC);
            brMain->SetColor(useAnimColors ? anim.cpuTempCol : PickTempColor(snap.cpuTempC, pal));
            DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 100, cy + 8, cx + cardW - 12, cy + 24), brMain, true);
        }

        D2D1_COLOR_F cpuCol = useAnimColors ? anim.cpuCol : PickLoadColor(snap.cpuPercent, pal);
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f%%", snap.cpuPercent);
        brMain->SetColor(cpuCol);
        DrawTextFormatted(rt, buf, m_fmtFlyCardBig, D2D1::RectF(cx + 12, cy + 25, cx + 105, cy + 52), brMain, true);

        if (snap.hasCpuFreq) {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.2f GHz", snap.cpuFreqGhz);
            m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawTextFormatted(rt, buf, m_fmtFlySub, D2D1::RectF(cx + 95, cy + 31, cx + cardW - 12, cy + 48), brAux, true);
            m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        DrawSparkline(rt, m_d2dFactory, m_roundStroke, brMain,
            D2D1::RectF(cx + 12, cy + 58, cx + cardW - 12, cy + cardH - 12),
            history.count, [&](size_t i) { return history.GetOrdered(history.cpu, i); },
            100.0, cpuCol, true, slideOffset);
    }

    // Card 2: GPU
    {
        float cx = padX + cardW + gap, cy = topY;
        drawCardBox(cx, cy, cardW, cardH);
        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, L"图形处理器 GPU", m_fmtFlyCardLabel, D2D1::RectF(cx + 12, cy + 8, cx + cardW - 12, cy + 24), brAux, false);

        D2D1_COLOR_F gpuCol = useAnimColors ? anim.gpuCol : PickLoadColor(snap.gpuPercent, pal);
        if (snap.hasGpuUsage) _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f%%", snap.gpuPercent);
        else wcscpy_s(buf, L"--");
        brMain->SetColor(gpuCol);
        DrawTextFormatted(rt, buf, m_fmtFlyCardBig, D2D1::RectF(cx + 12, cy + 25, cx + 100, cy + 52), brMain, true);

        if (snap.hasGpuTemp) {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f °C", snap.gpuTempC);
            brMain->SetColor(useAnimColors ? anim.gpuTempCol : PickTempColor(snap.gpuTempC, pal));
            m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawTextFormatted(rt, buf, m_fmtFlySub, D2D1::RectF(cx + 95, cy + 31, cx + cardW - 12, cy + 48), brMain, true);
            m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        DrawSparkline(rt, m_d2dFactory, m_roundStroke, brMain,
            D2D1::RectF(cx + 12, cy + 58, cx + cardW - 12, cy + cardH - 12),
            history.count, [&](size_t i) { return history.GetOrdered(history.gpu, i); },
            100.0, gpuCol, true, slideOffset);
    }

    // Card 3: VRAM & RAM
    {
        float cx = padX, cy = topY + cardH + gap;
        drawCardBox(cx, cy, cardW, cardH);
        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, L"显存与物理内存", m_fmtFlyCardLabel, D2D1::RectF(cx + 12, cy + 8, cx + cardW - 12, cy + 24), brAux, false);

        // VRAM row
        DrawTextFormatted(rt, L"显存", m_fmtFlySub, D2D1::RectF(cx + 12, cy + 30, cx + 48, cy + 46), brAux, false);
        D2D1_COLOR_F vramCol = pal.label;
        if (snap.hasVram) {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f / %.1f GB", snap.vramUsedGb, snap.vramTotalGb);
            vramCol = useAnimColors ? anim.vramCol : PickLoadColor(snap.vramPercent, pal, true);
        } else {
            wcscpy_s(buf, L"不可用");
        }
        brMain->SetColor(vramCol);
        DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 46, cy + 30, cx + cardW - 12, cy + 46), brMain, true);
        brAux->SetColor(pal.trackBg);
        DrawMiniBar(rt, cx + 12, cy + 50, cardW - 24, 5.0f, snap.hasVram ? dispVramPct : 0.0, brAux, brMain);

        // RAM row
        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, L"内存", m_fmtFlySub, D2D1::RectF(cx + 12, cy + 66, cx + 48, cy + 82), brAux, false);
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f / %.1f GB", snap.ramUsedGb, snap.ramTotalGb);
        D2D1_COLOR_F ramCol = useAnimColors ? anim.ramCol : PickLoadColor(snap.ramPercent, pal, true);
        brMain->SetColor(ramCol);
        DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 46, cy + 66, cx + cardW - 12, cy + 82), brMain, true);
        brAux->SetColor(pal.trackBg);
        DrawMiniBar(rt, cx + 12, cy + 86, cardW - 24, 5.0f, dispRamPct, brAux, brMain);
    }

    // Card 4: Network
    {
        float cx = padX + cardW + gap, cy = topY + cardH + gap;
        drawCardBox(cx, cy, cardW, cardH);
        const wchar_t* nicLabel = snap.nicName.empty() ? L"网络吞吐" : snap.nicName.c_str();
        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, nicLabel, m_fmtFlyCardLabel, D2D1::RectF(cx + 12, cy + 8, cx + cardW - 12, cy + 24), brAux, false);

        // Up/Down values
        brMain->SetColor(pal.netUp);
        DrawVectorArrow(rt, m_roundStroke, cx + 16, cy + 36, true, brMain);
        FormatSpeedCompact(snap.netUpBps, buf, _countof(buf), true);
        DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 24, cy + 28, cx + cardW - 12, cy + 44), brMain, true);

        brMain->SetColor(pal.netDown);
        DrawVectorArrow(rt, m_roundStroke, cx + 16, cy + 54, false, brMain);
        FormatSpeedCompact(snap.netDownBps, buf, _countof(buf), true);
        DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 24, cy + 46, cx + cardW - 12, cy + 62), brMain, true);

        double maxNet = anim.initialized ? std::max(10240.0, anim.netFlyoutMax) : 10240.0;
        if (!anim.initialized) {
            for (size_t i = 0; i < history.count; ++i) {
                maxNet = std::max(maxNet, static_cast<double>(std::max(
                    history.GetOrdered(history.netDown, i),
                    history.GetOrdered(history.netUp, i))));
            }
        }
        DrawSparkline(rt, m_d2dFactory, m_roundStroke, brMain,
            D2D1::RectF(cx + 12, cy + 68, cx + cardW - 12, cy + cardH - 12),
            history.count, [&](size_t i) { return history.GetOrdered(history.netUp, i); },
            maxNet, pal.netUp, true, slideOffset);
        DrawSparkline(rt, m_d2dFactory, m_roundStroke, brMain,
            D2D1::RectF(cx + 12, cy + 68, cx + cardW - 12, cy + cardH - 12),
            history.count, [&](size_t i) { return history.GetOrdered(history.netDown, i); },
            maxNet, pal.netDown, false, slideOffset);
    }

    // Bottom quick buttons (4 pills)
    float btnY = topY + (cardH + gap) * 2.0f + 4.0f;
    float btnH = 28.0f;
    float btnGap = 8.0f;
    float btnW = (innerW - padX * 2.0f - btnGap * 3.0f) / 4.0f;

    const wchar_t* themeLabel = (cfg.theme == ThemeMode::Auto) ? L"主题: 自动"
        : ((cfg.theme == ThemeMode::Light) ? L"主题: 浅色" : L"主题: 深色");
    const wchar_t* styleLabel = (cfg.style == VisualStyle::Capsule) ? L"风格: 胶囊" : L"风格: 无框";
    const wchar_t* barLabel = (cfg.style == VisualStyle::Capsule)
        ? (cfg.showMiniBars ? L"进度条: 开" : L"进度条: 关")
        : (cfg.showMiniBars ? L"微波形: 开" : L"微波形: 关");
    const wchar_t* moreLabel = L"更多设置 ▾";
    const wchar_t* btnLabels[4] = { styleLabel, themeLabel, barLabel, moreLabel };

    D2D1_COLOR_F btnPressCol = light ? Rgba(186, 198, 214, 0.96f) : Rgba(255, 255, 255, 0.06f);
    D2D1_COLOR_F btnHoverBorder = light ? Rgba(15, 23, 42, 0.12f) : Rgba(255, 255, 255, 0.14f);

    brAux->SetColor(pal.btnText);
    for (int i = 0; i < 4; ++i) {
        float bx = padX + i * (btnW + btnGap);
        float hT = std::clamp(flyAnim.btnHover[i], 0.0f, 1.0f);
        float pT = std::clamp(flyAnim.btnPress[i], 0.0f, 1.0f);

        D2D1_COLOR_F bg = anim::LerpColor(pal.btnBg, pal.btnHover, hT);
        bg = anim::LerpColor(bg, btnPressCol, pT);

        // Subtle tactile press inset (0.6 dip)
        float inset = pT * 0.6f;
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(bx + inset, btnY + inset, bx + btnW - inset, btnY + btnH - inset), 6.0f, 6.0f);
        brMain->SetColor(bg);
        rt->FillRoundedRectangle(&rr, brMain);

        if (hT > 0.02f) {
            D2D1_COLOR_F bc = btnHoverBorder;
            bc.a *= hT;
            brMain->SetColor(bc);
            rt->DrawRoundedRectangle(&rr, brMain, 1.0f);
        }

        DrawTextFormatted(rt, btnLabels[i], m_fmtFlyBtn, D2D1::RectF(bx + 2, btnY, bx + btnW - 2, btnY + btnH), brAux, false);

        if (outActionRectsPx) {
            outActionRectsPx[i] = RECT{
                static_cast<LONG>(std::lround((padLeft + bx) * scale)),
                static_cast<LONG>(std::lround((padTop + btnY) * scale)),
                static_cast<LONG>(std::lround((padLeft + bx + btnW) * scale)),
                static_cast<LONG>(std::lround((padTop + btnY + btnH) * scale)),
            };
        }
    }

    // Subtle footer hint
    wchar_t sessionBuf[96]{};
    wchar_t downTot[32]{}, upTot[32]{};
    FormatBytesShort(snap.netSessionDownBytes, downTot, _countof(downTot));
    FormatBytesShort(snap.netSessionUpBytes, upTot, _countof(upTot));
    _snwprintf_s(sessionBuf, _countof(sessionBuf), _TRUNCATE, L"本次累计 ↓ %ls · ↑ %ls   |   Alt+拖动可微调位置", downTot, upTot);
    brAux->SetColor(pal.label);
    m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawTextFormatted(rt, sessionBuf, m_fmtFlySub, D2D1::RectF(padX, btnY + btnH + 4.0f, innerW - padX, innerH - 6.0f), brAux, true);
    m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    rt->SetTransform(prevTransform);
}

bool RenderEngine::RenderFlyoutToWindow(
    HWND hwnd,
    POINT ptScreen,
    UINT dpi,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    const AnimatedMetrics& anim,
    const FlyoutAnimState& flyAnim,
    RECT outActionRectsPx[4]) {
    const WidgetDimensions dim = FlyoutDimensions();
    float scale = static_cast<float>(dpi) / 96.0f;
    int wPx = std::max(1, static_cast<int>(std::lround(dim.widthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(dim.heightDip * scale)));

    if (UsesDCompWindow(hwnd)) {
        SyncPopupWindowBounds(hwnd, ptScreen, wPx, hPx);
        return RenderToDCompSurface(hwnd, wPx, hPx, dpi, [&](ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brMain, ID2D1SolidColorBrush* brAux) {
            DrawFlyoutContent(rt, brMain, brAux, dpi, cfg, snap, history, anim, flyAnim, outActionRectsPx);
        });
    }

    if (!EnsureWicTarget(m_flyoutTarget, wPx, hPx, dpi)) return false;

    ID2D1RenderTarget* rt = m_flyoutTarget.rt;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawFlyoutContent(rt, m_flyoutTarget.brMain, m_flyoutTarget.brAux, dpi, cfg, snap, history, anim, flyAnim, outActionRectsPx);
    if (FAILED(rt->EndDraw())) return false;
    return PresentWicToWindow(hwnd, ptScreen, m_flyoutTarget.bitmap, wPx, hPx);
}
