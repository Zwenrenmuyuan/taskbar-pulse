#include "render/render_engine.hpp"
#include "render/palette.hpp"
#include "render/draw_primitives.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

WidgetDimensions RenderEngine::MeasureWidget(const AppConfig& cfg, float maxHeightDip) const {
    float h = std::clamp(maxHeightDip, 28.0f, 40.0f);
    if (cfg.style == VisualStyle::Capsule) {
        float w = 356.0f;
        if (cfg.showGoldPrice) w += 82.0f;
        return { w, h };
    }
    float w = 362.0f;
    if (cfg.showGoldPrice) w += 82.0f;
    return { w, h };
}

void RenderEngine::DrawWidgetContent(
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
    float pressProgress) {
    (void)history;
    const bool light = ResolveEffectiveLightTheme(cfg);
    const bool capsule = (cfg.style == VisualStyle::Capsule);
    const Palette pal = MakePalette(light, !capsule);
    const bool showBars = cfg.showMiniBars;

    IDWriteTextFormat* fmtLabel = capsule ? m_fmtLabel : m_fmtMinLabel;
    IDWriteTextFormat* fmtValue = capsule ? m_fmtValue : m_fmtMinValue;
    IDWriteTextFormat* fmtSubVal = m_fmtMinSubVal;

    const float hoverT = std::clamp(hoverProgress, 0.0f, 1.0f);
    const float pressT = std::clamp(pressProgress, 0.0f, 1.0f);
    const bool useAnimColors = anim.colorsInitialized &&
        anim.lastLightTheme == light &&
        anim.lastMinimalStyle == !capsule;

    if (capsule) {
        D2D1_COLOR_F bgCol = anim::LerpColor(pal.capsuleBg, pal.capsuleBgHover, hoverT);
        bgCol = anim::LerpColor(bgCol, pal.capsuleBgPressed, pressT);

        D2D1_COLOR_F borderHover = light ? Rgba(20, 20, 24, 0.14f) : Rgba(255, 255, 255, 0.18f);
        D2D1_COLOR_F borderCol = anim::LerpColor(pal.capsuleBorder, borderHover, std::max(hoverT, pressT));

        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, dim.widthDip - 0.5f, dim.heightDip - 0.5f), 8.5f, 8.5f);
        brMain->SetColor(bgCol);
        rt->FillRoundedRectangle(&rr, brMain);
        brMain->SetColor(borderCol);
        rt->DrawRoundedRectangle(&rr, brMain, 1.0f);

        // Subtle top glass bevel
        brMain->SetColor(pal.capsuleHighlight);
        rt->DrawLine(D2D1::Point2F(10.0f, 1.5f), D2D1::Point2F(dim.widthDip - 10.0f, 1.5f), brMain, 1.0f);

        // Column dividers
        brMain->SetColor(pal.divider);
        float divX[] = { 89.5f, 169.5f, 276.5f };
        for (float dx : divX) {
            rt->DrawLine(D2D1::Point2F(dx, 8.0f), D2D1::Point2F(dx, dim.heightDip - 8.0f), brMain, 1.0f);
        }
        if (cfg.showGoldPrice) {
            rt->DrawLine(D2D1::Point2F(352.5f, 8.0f), D2D1::Point2F(352.5f, dim.heightDip - 8.0f), brMain, 1.0f);
        }
    } else {
        float pillT = std::max(hoverT, pressT);
        if (pillT > 0.005f) {
            // Smooth Fluent hover/press wash during pointer interaction
            D2D1_COLOR_F hoverBg = light ? Rgba(255, 255, 255, 0.64f) : Rgba(255, 255, 255, 0.072f);
            D2D1_COLOR_F pressBg = light ? Rgba(255, 255, 255, 0.82f) : Rgba(255, 255, 255, 0.115f);
            D2D1_COLOR_F hc = anim::LerpColor(hoverBg, pressBg, pressT);
            hc.a *= pillT;

            D2D1_COLOR_F bc = light ? Rgba(20, 20, 24, 0.068f) : Rgba(255, 255, 255, 0.09f);
            bc.a *= pillT;

            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0.5f, 1.0f, dim.widthDip - 0.5f, dim.heightDip - 1.0f), 7.0f, 7.0f);
            brMain->SetColor(hc);
            rt->FillRoundedRectangle(&rr, brMain);
            brMain->SetColor(bc);
            rt->DrawRoundedRectangle(&rr, brMain, 1.0f);
        }

        // T1-Pro: whisper-quiet short hairline dividers separating each metric group
        brMain->SetColor(pal.divider);
        float divX[] = { 86.5f, 167.5f, 280.5f };
        for (float dx : divX) {
            rt->DrawLine(D2D1::Point2F(dx, 9.0f), D2D1::Point2F(dx, dim.heightDip - 9.0f), brMain, 1.0f);
        }
        if (cfg.showGoldPrice) {
            rt->DrawLine(D2D1::Point2F(361.5f, 9.0f), D2D1::Point2F(361.5f, dim.heightDip - 9.0f), brMain, 1.0f);
        }
    }

    // Column geometry (T1-Pro frameless & refreshed Capsule)
    struct ColBox { float x; float w; };
    ColBox cols[4] = {
        capsule ? ColBox{ 10.0f, 74.0f } : ColBox{ 6.0f, 74.0f },
        capsule ? ColBox{ 96.0f, 68.0f } : ColBox{ 93.0f, 68.0f },
        capsule ? ColBox{ 176.0f, 94.0f } : ColBox{ 174.0f, 100.0f },
        capsule ? ColBox{ 283.0f, 64.0f } : ColBox{ 287.0f, 68.0f },
    };

    const float rowH = 14.0f;
    const float yRow1 = showBars ? 3.5f : 5.0f;
    const float yRow2 = showBars ? 18.5f : 20.5f;
    const float yCenter = (dim.heightDip - rowH) * 0.5f;
    const float barH = capsule ? 2.0f : 1.8f;
    const float yBar = dim.heightDip - barH - (capsule ? 2.8f : 2.2f);

    const double dispCpuPct = anim.initialized ? anim.cpuPct : snap.cpuPercent;
    const double dispGpuPct = anim.initialized ? anim.gpuPct : (snap.hasGpuUsage ? snap.gpuPercent : 0.0);
    const double dispVramPct = anim.initialized ? anim.vramPct : (snap.hasVram ? snap.vramPercent : 0.0);
    const double dispRamPct = anim.initialized ? anim.ramPct : snap.ramPercent;

    // Quiet neutral micro-bar fill for T1-Pro normal load (elevates to warn/hot on high load)
    auto pickTaskbarBarColor = [&](double pct, const D2D1_COLOR_F& capsuleSignal, bool memStyle = false) -> D2D1_COLOR_F {
        double hotThresh = memStyle ? 90.0 : 88.0;
        double warnThresh = memStyle ? 78.0 : 70.0;
        if (pct >= hotThresh) return pal.hot;
        if (pct >= warnThresh) return pal.warn;
        if (capsule) return capsuleSignal;
        D2D1_COLOR_F quiet = pal.muted;
        quiet.a = light ? 0.50f : 0.55f;
        return quiet;
    };

    wchar_t valBuf[48]{};

    // ---- Col 1: CPU ----
    {
        const auto& c = cols[0];
        D2D1_COLOR_F cpuCol = useAnimColors ? anim.cpuCol : PickLoadColor(snap.cpuPercent, pal);

        bool hasRow2 = (cfg.row2Col1 != Row2Col1Mode::Hide);
        float r1y = hasRow2 ? yRow1 : (showBars ? 9.5f : yCenter);

        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, L"CPU", fmtLabel, D2D1::RectF(c.x, r1y, c.x + c.w, r1y + rowH), brAux, false);
        _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1f%%", snap.cpuPercent);
        brMain->SetColor(cpuCol);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, r1y, c.x + c.w, r1y + rowH), brMain, true);

        if (cfg.row2Col1 == Row2Col1Mode::CpuFreq) {
            DrawTextFormatted(rt, L"主频", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brAux, false);
            if (snap.hasCpuFreq) {
                _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.2fG", snap.cpuFreqGhz);
            } else {
                wcscpy_s(valBuf, L"--");
            }
            brMain->SetColor(pal.label);
            DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
        } else if (cfg.row2Col1 == Row2Col1Mode::CpuTemp) {
            DrawTextFormatted(rt, L"温度", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brAux, false);
            if (snap.hasCpuTemp) {
                _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f°C", snap.cpuTempC);
            } else {
                wcscpy_s(valBuf, L"--");
            }
            D2D1_COLOR_F tempCol = snap.hasCpuTemp
                ? (useAnimColors ? anim.cpuTempCol : PickTempColor(snap.cpuTempC, pal))
                : pal.label;
            brMain->SetColor(tempCol);
            DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
        } else if (cfg.row2Col1 == Row2Col1Mode::MemPct) {
            DrawTextFormatted(rt, L"占存", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brAux, false);
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1f%%", snap.ramPercent);
            brMain->SetColor(useAnimColors ? anim.ramCol : PickLoadColor(snap.ramPercent, pal, true));
            DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
        }

        if (showBars) {
            brAux->SetColor(pal.trackBg);
            brMain->SetColor(pickTaskbarBarColor(snap.cpuPercent, useAnimColors ? anim.cpuSignalCol : pal.accentCpu));
            DrawMiniBar(rt, c.x, yBar, c.w, barH, std::max(4.0, dispCpuPct), brAux, brMain);
        }
    }

    // ---- Col 2: GPU ----
    {
        const auto& c = cols[1];
        D2D1_COLOR_F gpuCol = useAnimColors ? anim.gpuCol : PickLoadColor(snap.gpuPercent, pal);

        bool hasTemp = cfg.showGpuTemp && snap.hasGpuTemp;
        float r1y = hasTemp ? yRow1 : (showBars ? 9.5f : yCenter);

        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, L"GPU", fmtLabel, D2D1::RectF(c.x, r1y, c.x + c.w, r1y + rowH), brAux, false);
        if (snap.hasGpuUsage) {
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f%%", snap.gpuPercent);
        } else {
            wcscpy_s(valBuf, L"--");
        }
        brMain->SetColor(gpuCol);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, r1y, c.x + c.w, r1y + rowH), brMain, true);

        if (hasTemp) {
            DrawTextFormatted(rt, L"温度", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brAux, false);
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f°C", snap.gpuTempC);
            brMain->SetColor(useAnimColors ? anim.gpuTempCol : PickTempColor(snap.gpuTempC, pal));
            DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
        }

        if (showBars) {
            brAux->SetColor(pal.trackBg);
            brMain->SetColor(pickTaskbarBarColor(snap.gpuPercent, useAnimColors ? anim.gpuSignalCol : pal.accentGpu));
            DrawMiniBar(rt, c.x, yBar, c.w, barH, snap.hasGpuUsage ? std::max(4.0, dispGpuPct) : 0.0, brAux, brMain);
        }
    }

    // ---- Col 3: VRAM & RAM ----
    {
        const auto& c = cols[2];
        D2D1_COLOR_F vramCol = snap.hasVram
            ? (useAnimColors ? anim.vramCol : PickLoadColor(snap.vramPercent, pal, true))
            : pal.label;
        D2D1_COLOR_F ramCol = useAnimColors ? anim.ramCol : PickLoadColor(snap.ramPercent, pal, true);

        brAux->SetColor(pal.label);
        auto drawDualMemRow = [&](float rowY, const wchar_t* label, bool hasVal, double usedGb, double pct, const D2D1_COLOR_F& col) {
            DrawTextFormatted(rt, label, fmtLabel, D2D1::RectF(c.x, rowY, c.x + c.w, rowY + rowH), brAux, false);
            if (!hasVal) {
                brMain->SetColor(pal.label);
                DrawTextFormatted(rt, L"--", fmtValue, D2D1::RectF(c.x, rowY, c.x + c.w, rowY + rowH), brMain, true);
                return;
            }
            wchar_t pctBuf[16]{};
            _snwprintf_s(pctBuf, _countof(pctBuf), _TRUNCATE, L"%.0f%%", pct);
            float pctW = MeasureTextWidth(pctBuf, fmtValue, true);

            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1fG", usedGb);
            float subRight = c.x + c.w - pctW - 4.5f;
            if (subRight > c.x + 26.0f) {
                DrawTextFormatted(rt, valBuf, m_fmtMinSub, D2D1::RectF(c.x + 24.0f, rowY + 0.3f, subRight, rowY + rowH + 0.3f), brAux, true);
            }
            brMain->SetColor(col);
            DrawTextFormatted(rt, pctBuf, fmtValue, D2D1::RectF(c.x, rowY, c.x + c.w, rowY + rowH), brMain, true);
        };

        drawDualMemRow(yRow1, L"显存", snap.hasVram, snap.vramUsedGb, snap.vramPercent, vramCol);
        drawDualMemRow(yRow2, L"内存", true, snap.ramUsedGb, snap.ramPercent, ramCol);

        if (showBars && capsule) {
            float halfW = (c.w - 4.0f) * 0.5f;
            brAux->SetColor(pal.trackBg);
            brMain->SetColor(pickTaskbarBarColor(snap.vramPercent, useAnimColors ? anim.vramSignalCol : pal.accentVram, true));
            DrawMiniBar(rt, c.x, yBar, halfW, barH, snap.hasVram ? dispVramPct : 0.0, brAux, brMain);
            brMain->SetColor(pickTaskbarBarColor(snap.ramPercent, useAnimColors ? anim.ramSignalCol : pal.accentRam, true));
            DrawMiniBar(rt, c.x + halfW + 4.0f, yBar, halfW, barH, dispRamPct, brAux, brMain);
        }
    }

    // ---- Col 4: Network Up & Down ----
    {
        const auto& c = cols[3];
        float arrowX = c.x + 3.5f;

        // Upload row: quiet neutral secondary weight (arrow lights up subtly under real traffic)
        bool upHigh = snap.netUpBps >= 20480.0;
        brMain->SetColor(upHigh ? pal.netUp : pal.label);
        DrawVectorArrow(rt, m_roundStroke, arrowX, yRow1 + rowH * 0.5f, true, brMain);
        FormatSpeedCompact(snap.netUpBps, valBuf, _countof(valBuf), false);
        D2D1_COLOR_F upTxt = pal.labelStrong;
        upTxt.a = light ? 0.80f : 0.82f;
        brMain->SetColor(upTxt);
        DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x + 10.0f, yRow1, c.x + c.w, yRow1 + rowH), brMain, true);

        // Download row: primary ink weight
        bool downHigh = snap.netDownBps >= 20480.0;
        brMain->SetColor(downHigh ? pal.netDown : pal.label);
        DrawVectorArrow(rt, m_roundStroke, arrowX, yRow2 + rowH * 0.5f, false, brMain);
        FormatSpeedCompact(snap.netDownBps, valBuf, _countof(valBuf), false);
        brMain->SetColor(pal.labelStrong);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x + 10.0f, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);

        if (showBars && capsule) {
            double logPct = anim.initialized ? anim.netLogPct : 0.0;
            if (!anim.initialized) {
                double peakBps = std::max(snap.netDownBps, snap.netUpBps);
                if (peakBps > 512.0) {
                    logPct = std::clamp((std::log10(peakBps / 512.0) / 5.3) * 100.0, 4.0, 100.0);
                }
            }
            brAux->SetColor(pal.trackBg);
            brMain->SetColor(snap.netDownBps >= snap.netUpBps ? pal.netDown : pal.netUp);
            DrawMiniBar(rt, c.x, yBar, c.w, barH, logPct, brAux, brMain);
        }
    }

    // ---- Col 5: International gold, CNY per gram (harmonized 2-row T1-Pro grammar) ----
    if (cfg.showGoldPrice) {
        float x = capsule ? 359.0f : 368.0f;
        float w = 70.0f;

        // Row 1: "金价" (muted left) + primary neutral price "771.0" (bold right, matching CPU/GPU/VRAM baseline)
        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, L"金价", fmtLabel, D2D1::RectF(x, yRow1, x + 26.0f, yRow1 + rowH), brAux, false);
        if (gold.cnyPerGram > 0.0) {
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1f", gold.cnyPerGram);
        } else {
            wcscpy_s(valBuf, L"--");
        }
        brMain->SetColor(gold.cnyPerGram > 0.0 ? pal.labelStrong : pal.label);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(x + 24.0f, yRow1, x + w, yRow1 + rowH), brMain, true);

        // Row 2: "元/克" (muted left) + fresh quote time "11:05" (or "待更新") right, eliminating bottom-left void
        brAux->SetColor(pal.label);
        DrawTextFormatted(rt, L"元/克", fmtLabel, D2D1::RectF(x, yRow2, x + 32.0f, yRow2 + rowH), brAux, false);
        if (gold.cnyPerGram > 0.0 && gold.fetchSucceeded && gold.updatedLocal.wYear > 0) {
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%02u:%02u",
                static_cast<unsigned>(gold.updatedLocal.wHour),
                static_cast<unsigned>(gold.updatedLocal.wMinute));
        } else if (gold.cnyPerGram > 0.0 && !gold.fetchSucceeded) {
            wcscpy_s(valBuf, L"待更新");
        } else {
            wcscpy_s(valBuf, L"--");
        }
        brMain->SetColor(pal.label);
        DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(x + 30.0f, yRow2, x + w, yRow2 + rowH), brMain, true);
    }
}

bool RenderEngine::RenderWidgetToWindow(
    HWND hwnd,
    POINT ptScreen,
    UINT dpi,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    const GoldQuote& gold,
    const AnimatedMetrics& anim,
    float hoverProgress,
    float pressProgress) {
    WidgetDimensions dim = MeasureWidget(cfg);
    float scale = static_cast<float>(dpi) / 96.0f;
    int wPx = std::max(1, static_cast<int>(std::lround(dim.widthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(dim.heightDip * scale)));

    if (UsesDCompWindow(hwnd)) {
        return RenderToDCompSurface(hwnd, wPx, hPx, dpi, [&](ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brMain, ID2D1SolidColorBrush* brAux) {
            DrawWidgetContent(rt, brMain, brAux, dim, cfg, snap, history, gold, anim, hoverProgress, pressProgress);
        });
    }

    if (!EnsureWicTarget(m_widgetTarget, wPx, hPx, dpi)) return false;

    ID2D1RenderTarget* rt = m_widgetTarget.rt;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawWidgetContent(rt, m_widgetTarget.brMain, m_widgetTarget.brAux, dim, cfg, snap, history, gold, anim, hoverProgress, pressProgress);
    if (FAILED(rt->EndDraw())) return false;
    return hwnd ? PresentWicToWindow(hwnd, ptScreen, m_widgetTarget.bitmap, wPx, hPx) : true;
}
