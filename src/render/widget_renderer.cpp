#include "render/render_engine.hpp"
#include "render/palette.hpp"
#include "render/draw_primitives.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

WidgetDimensions RenderEngine::MeasureWidget(const AppConfig& cfg, float maxHeightDip) const {
    float h = std::clamp(maxHeightDip, 28.0f, 40.0f);
    // S1 (B1-matched Swiss Icons + Segmented Ticks): compact 344 dip (+82 gold), Capsule adds 8 dip shell pad
    float w = (cfg.style == VisualStyle::Capsule) ? 352.0f : 344.0f;
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
    const float pad = capsule ? 4.0f : 0.0f;

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

            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0.5f, 1.0f, dim.widthDip - 0.5f, dim.heightDip - 1.0f), 7.5f, 7.5f);
            brMain->SetColor(hc);
            rt->FillRoundedRectangle(&rr, brMain);
            brMain->SetColor(bc);
            rt->DrawRoundedRectangle(&rr, brMain, 1.0f);
        }
    }

    // Whisper-quiet short hairline dividers separating each metric group
    brMain->SetColor(pal.divider);
    const float divX[3] = { pad + 79.5f, pad + 152.5f, pad + 266.5f };
    for (float dx : divX) {
        rt->DrawLine(D2D1::Point2F(dx, 9.0f), D2D1::Point2F(dx, dim.heightDip - 9.0f), brMain, 1.0f);
    }
    if (cfg.showGoldPrice) {
        rt->DrawLine(D2D1::Point2F(pad + 341.5f, 9.0f), D2D1::Point2F(pad + 341.5f, dim.heightDip - 9.0f), brMain, 1.0f);
    }

    // S1 column geometry (B1-matched compact icons + segmented ticks)
    struct ColBox { float x; float w; };
    const ColBox cols[4] = {
        { pad + 8.0f, 65.0f },   // CPU: 8..73
        { pad + 87.0f, 59.0f },  // GPU: 87..146
        { pad + 160.0f, 100.0f },// VRAM & RAM: 160..260
        { pad + 274.0f, 61.0f }, // Network: 274..335
    };

    const float rowH = 14.0f;
    const float yRow1 = showBars ? 3.0f : 5.0f;
    const float yRow2 = showBars ? 17.5f : 20.5f;
    const float yCenter = (dim.heightDip - rowH) * 0.5f;
    const float barH = 2.6f;
    const float yBar = dim.heightDip - barH - (capsule ? 3.2f : 2.6f);

    const double dispCpuPct = anim.initialized ? anim.cpuPct : snap.cpuPercent;
    // GPU and VRAM display whole percentages. Clear their meters as soon as
    // the readout reaches zero, including while an earlier reading fades out.
    const double dispGpuPct = (snap.hasGpuUsage && std::nearbyint(snap.gpuPercent) > 0.0)
        ? (anim.initialized ? anim.gpuPct : snap.gpuPercent) : 0.0;
    const double dispVramPct = (snap.hasVram && std::nearbyint(snap.vramPercent) > 0.0)
        ? (anim.initialized ? anim.vramPct : snap.vramPercent) : 0.0;
    const double dispRamPct = anim.initialized ? anim.ramPct : snap.ramPercent;

    wchar_t valBuf[48]{};

    // ---- Col 1: CPU (Micro chip icon in Teal + neutral bold readout + 10-seg B1 ticks) ----
    {
        const auto& c = cols[0];
        D2D1_COLOR_F cpuCol = useAnimColors ? anim.cpuCol : PickLoadColor(snap.cpuPercent, pal);
        D2D1_COLOR_F cpuSigCol = useAnimColors ? anim.cpuSignalCol : PickSignalColor(snap.cpuPercent, pal.accentCpu, pal);

        bool hasRow2 = (cfg.row2Col1 != Row2Col1Mode::Hide);
        float r1y = hasRow2 ? yRow1 : (showBars ? 10.0f : yCenter);

        brMain->SetColor(cpuSigCol);
        DrawCpuIcon(rt, m_roundStroke, c.x + 5.5f, r1y + rowH * 0.5f, brMain, 0.84f);

        _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1f%%", snap.cpuPercent);
        brMain->SetColor(cpuCol);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x + 13.0f, r1y, c.x + c.w, r1y + rowH), brMain, true);

        brAux->SetColor(pal.muted);
        if (cfg.row2Col1 == Row2Col1Mode::CpuFreq) {
            DrawTextFormatted(rt, L"CPU", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + 28.0f, yRow2 + rowH), brAux, false);
            if (snap.hasCpuFreq) {
                _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.2fG", snap.cpuFreqGhz);
            } else {
                wcscpy_s(valBuf, L"--");
            }
            brMain->SetColor(pal.label);
            DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x + 24.0f, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
        } else if (cfg.row2Col1 == Row2Col1Mode::CpuTemp) {
            DrawTextFormatted(rt, L"CPU", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + 28.0f, yRow2 + rowH), brAux, false);
            if (snap.hasCpuTemp) {
                _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f°C", snap.cpuTempC);
            } else {
                wcscpy_s(valBuf, L"--");
            }
            D2D1_COLOR_F tempCol = snap.hasCpuTemp
                ? (useAnimColors ? anim.cpuTempCol : PickTempColor(snap.cpuTempC, pal))
                : pal.label;
            brMain->SetColor(tempCol);
            DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x + 24.0f, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
        } else if (cfg.row2Col1 == Row2Col1Mode::MemPct) {
            DrawTextFormatted(rt, L"占存", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + 28.0f, yRow2 + rowH), brAux, false);
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1f%%", snap.ramPercent);
            brMain->SetColor(useAnimColors ? anim.ramCol : PickLoadColor(snap.ramPercent, pal, true));
            DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x + 24.0f, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
        }

        if (showBars) {
            DrawSegmentedMeter(rt, c.x, yBar, c.w, barH, dispCpuPct, 10, pal.trackBg, cpuSigCol, brMain, 1.6f);
        }
    }

    // ---- Col 2: GPU (Micro GPU icon in Cobalt + neutral bold readout + 9-seg B1 ticks) ----
    {
        const auto& c = cols[1];
        D2D1_COLOR_F gpuCol = useAnimColors ? anim.gpuCol : PickLoadColor(snap.gpuPercent, pal);
        D2D1_COLOR_F gpuSigCol = useAnimColors ? anim.gpuSignalCol : PickSignalColor(snap.gpuPercent, pal.accentGpu, pal);

        bool hasTemp = cfg.showGpuTemp && snap.hasGpuTemp;
        float r1y = hasTemp ? yRow1 : (showBars ? 10.0f : yCenter);

        brMain->SetColor(snap.hasGpuUsage ? gpuSigCol : pal.muted);
        DrawGpuIcon(rt, m_roundStroke, c.x + 5.5f, r1y + rowH * 0.5f, brMain, 0.86f);

        if (snap.hasGpuUsage) {
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f%%", snap.gpuPercent);
        } else {
            wcscpy_s(valBuf, L"--");
        }
        brMain->SetColor(gpuCol);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x + 13.0f, r1y, c.x + c.w, r1y + rowH), brMain, true);

        if (hasTemp) {
            brAux->SetColor(pal.muted);
            DrawTextFormatted(rt, L"GPU", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + 28.0f, yRow2 + rowH), brAux, false);
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f°C", snap.gpuTempC);
            brMain->SetColor(useAnimColors ? anim.gpuTempCol : PickTempColor(snap.gpuTempC, pal));
            DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x + 24.0f, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
        }

        if (showBars) {
            DrawSegmentedMeter(rt, c.x, yBar, c.w, barH, dispGpuPct, 9,
                pal.trackBg, gpuSigCol, brMain, 1.6f);
        }
    }

    // ---- Col 3: VRAM & RAM (Dual 7+7 Indigo & Sky B1 segmented ticks) ----
    {
        const auto& c = cols[2];
        D2D1_COLOR_F vramCol = snap.hasVram
            ? (useAnimColors ? anim.vramCol : PickLoadColor(snap.vramPercent, pal, true))
            : pal.label;
        D2D1_COLOR_F ramCol = useAnimColors ? anim.ramCol : PickLoadColor(snap.ramPercent, pal, true);
        D2D1_COLOR_F vramSigCol = snap.hasVram
            ? (useAnimColors ? anim.vramSignalCol : PickSignalColor(snap.vramPercent, pal.accentVram, pal, true))
            : pal.muted;
        D2D1_COLOR_F ramSigCol = useAnimColors ? anim.ramSignalCol : PickSignalColor(snap.ramPercent, pal.accentRam, pal, true);

        auto drawDualMemRow = [&](float rowY, const wchar_t* label, bool hasVal, double usedGb, double pct, const D2D1_COLOR_F& col) {
            brAux->SetColor(pal.label);
            DrawTextFormatted(rt, label, fmtLabel, D2D1::RectF(c.x, rowY, c.x + 28.0f, rowY + rowH), brAux, false);
            if (!hasVal) {
                brMain->SetColor(pal.label);
                DrawTextFormatted(rt, L"--", fmtValue, D2D1::RectF(c.x + 26.0f, rowY, c.x + c.w, rowY + rowH), brMain, true);
                return;
            }
            wchar_t pctBuf[16]{};
            _snwprintf_s(pctBuf, _countof(pctBuf), _TRUNCATE, L"%.0f%%", pct);
            float pctW = MeasureTextWidth(pctBuf, fmtValue, true);

            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1fG", usedGb);
            float subRight = c.x + c.w - pctW - 4.0f;
            if (subRight > c.x + 26.0f) {
                brAux->SetColor(pal.muted);
                DrawTextFormatted(rt, valBuf, m_fmtMinSub, D2D1::RectF(c.x + 24.0f, rowY + 0.3f, subRight, rowY + rowH + 0.3f), brAux, true);
            }
            brMain->SetColor(col);
            DrawTextFormatted(rt, pctBuf, fmtValue, D2D1::RectF(c.x + 26.0f, rowY, c.x + c.w, rowY + rowH), brMain, true);
        };

        drawDualMemRow(yRow1, L"显存", snap.hasVram, snap.vramUsedGb, snap.vramPercent, vramCol);
        drawDualMemRow(yRow2, L"内存", true, snap.ramUsedGb, snap.ramPercent, ramCol);

        if (showBars) {
            const float halfW = (c.w - 6.0f) * 0.5f;
            DrawSegmentedMeter(rt, c.x, yBar, halfW, barH, dispVramPct, 7,
                pal.trackBg, vramSigCol, brMain, 1.6f);
            DrawSegmentedMeter(rt, c.x + halfW + 6.0f, yBar, halfW, barH, dispRamPct, 7,
                pal.trackBg, ramSigCol, brMain, 1.6f);
        }
    }

    // ---- Col 4: Network Up & Down (Restrained Cobalt/Emerald arrows + neutral speeds) ----
    {
        const auto& c = cols[3];
        float arrowX = c.x + 3.5f;

        bool upHigh = snap.netUpBps >= 20480.0;
        D2D1_COLOR_F upArrCol = pal.netUp;
        if (!upHigh) upArrCol.a *= 0.82f;
        brMain->SetColor(upArrCol);
        DrawVectorArrow(rt, m_roundStroke, arrowX, yRow1 + rowH * 0.5f, true, brMain);
        FormatSpeedCompact(snap.netUpBps, valBuf, _countof(valBuf), false);
        D2D1_COLOR_F upTxt = pal.labelStrong;
        upTxt.a = light ? 0.78f : 0.80f;
        brMain->SetColor(upTxt);
        DrawTextFormatted(rt, valBuf, fmtSubVal, D2D1::RectF(c.x + 10.0f, yRow1, c.x + c.w, yRow1 + rowH), brMain, true);

        bool downHigh = snap.netDownBps >= 20480.0;
        D2D1_COLOR_F downArrCol = pal.netDown;
        if (!downHigh) downArrCol.a *= 0.82f;
        brMain->SetColor(downArrCol);
        DrawVectorArrow(rt, m_roundStroke, arrowX, yRow2 + rowH * 0.5f, false, brMain);
        FormatSpeedCompact(snap.netDownBps, valBuf, _countof(valBuf), false);
        brMain->SetColor(pal.labelStrong);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x + 10.0f, yRow2, c.x + c.w, yRow2 + rowH), brMain, true);
    }

    // ---- Col 5: International Gold (Warm gold ingot micro-icon + neutral price + time) ----
    if (cfg.showGoldPrice) {
        const float x = pad + 349.0f;
        const float w = 70.0f;

        brMain->SetColor(gold.cnyPerGram > 0.0 ? pal.gold : pal.muted);
        DrawGoldIcon(rt, m_roundStroke, x + 5.8f, yRow1 + rowH * 0.5f, brMain, 0.96f);

        if (gold.cnyPerGram > 0.0) {
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1f", gold.cnyPerGram);
        } else {
            wcscpy_s(valBuf, L"--");
        }
        brMain->SetColor(gold.cnyPerGram > 0.0 ? pal.labelStrong : pal.label);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(x + 15.0f, yRow1, x + w, yRow1 + rowH), brMain, true);

        brAux->SetColor(pal.muted);
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
        brMain->SetColor(pal.muted);
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
