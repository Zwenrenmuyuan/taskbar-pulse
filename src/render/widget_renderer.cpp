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
        float w = 348.0f;
        if (cfg.showGoldPrice) w += 90.0f;
        return { w, h };
    }
    float w = 356.0f;
    if (cfg.showGoldPrice) w += 78.0f;
    return { w, h };
}

void RenderEngine::DrawWidgetContent(
    ID2D1RenderTarget* rt,
    const WidgetDimensions& dim,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    const GoldQuote& gold,
    bool isHovered,
    bool isPressed) {
    const bool light = ResolveEffectiveLightTheme(cfg);
    const bool capsule = (cfg.style == VisualStyle::Capsule);
    const Palette pal = MakePalette(light, !capsule);
    const bool showBars = cfg.showMiniBars && capsule;
    const bool showSpark = cfg.showMiniBars && !capsule;
    IDWriteTextFormat* fmtLabel = capsule ? m_fmtLabel : m_fmtMinLabel;
    IDWriteTextFormat* fmtValue = capsule ? m_fmtValue : m_fmtMinValue;

    ID2D1SolidColorBrush* brLabel = nullptr;
    ID2D1SolidColorBrush* brVal = nullptr;
    ID2D1SolidColorBrush* brTrack = nullptr;
    rt->CreateSolidColorBrush(pal.label, &brLabel);
    rt->CreateSolidColorBrush(pal.ok, &brVal);
    rt->CreateSolidColorBrush(pal.trackBg, &brTrack);

    if (capsule) {
        D2D1_COLOR_F bgCol = isPressed ? pal.capsuleBgPressed : (isHovered ? pal.capsuleBgHover : pal.capsuleBg);
        ID2D1SolidColorBrush* brBg = nullptr;
        ID2D1SolidColorBrush* brBorder = nullptr;
        ID2D1SolidColorBrush* brHi = nullptr;
        ID2D1SolidColorBrush* brDiv = nullptr;
        rt->CreateSolidColorBrush(bgCol, &brBg);
        rt->CreateSolidColorBrush(pal.capsuleBorder, &brBorder);
        rt->CreateSolidColorBrush(pal.capsuleHighlight, &brHi);
        rt->CreateSolidColorBrush(pal.divider, &brDiv);

        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, dim.widthDip - 0.5f, dim.heightDip - 0.5f), 8.0f, 8.0f);
        rt->FillRoundedRectangle(&rr, brBg);
        rt->DrawRoundedRectangle(&rr, brBorder, 1.0f);
        // Subtle top glass edge highlight
        rt->DrawLine(D2D1::Point2F(9.0f, 1.5f), D2D1::Point2F(dim.widthDip - 9.0f, 1.5f), brHi, 1.0f);

        // Column dividers
        float divX[] = { 91.5f, 172.5f, 267.5f };
        for (float dx : divX) {
            rt->DrawLine(D2D1::Point2F(dx, 7.0f), D2D1::Point2F(dx, dim.heightDip - 7.0f), brDiv, 1.0f);
        }
        if (cfg.showGoldPrice) {
            rt->DrawLine(D2D1::Point2F(343.5f, 7.0f), D2D1::Point2F(343.5f, dim.heightDip - 7.0f), brDiv, 1.0f);
        }

        SafeRelease(brBg);
        SafeRelease(brBorder);
        SafeRelease(brHi);
        SafeRelease(brDiv);
    } else if (isHovered || isPressed) {
        // Subtle Fluent hover/press pill ONLY during pointer interaction
        ID2D1SolidColorBrush* brHover = nullptr;
        ID2D1SolidColorBrush* brHoverBorder = nullptr;
        D2D1_COLOR_F hc = light
            ? (isPressed ? Rgba(255, 255, 255, 0.80f) : Rgba(255, 255, 255, 0.62f))
            : (isPressed ? Rgba(255, 255, 255, 0.105f) : Rgba(255, 255, 255, 0.065f));
        D2D1_COLOR_F bc = light ? Rgba(15, 23, 42, 0.07f) : Rgba(255, 255, 255, 0.09f);
        rt->CreateSolidColorBrush(hc, &brHover);
        rt->CreateSolidColorBrush(bc, &brHoverBorder);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0.5f, 1.0f, dim.widthDip - 0.5f, dim.heightDip - 1.0f), 6.0f, 6.0f);
        rt->FillRoundedRectangle(&rr, brHover);
        rt->DrawRoundedRectangle(&rr, brHoverBorder, 1.0f);
        SafeRelease(brHover);
        SafeRelease(brHoverBorder);
    }

    // Column geometry
    struct ColBox { float x; float w; };
    ColBox cols[4] = {
        capsule ? ColBox{ 10.0f, 76.0f } : ColBox{ 6.0f, 72.0f },
        capsule ? ColBox{ 97.0f, 70.0f } : ColBox{ 90.0f, 66.0f },
        capsule ? ColBox{ 178.0f, 84.0f } : ColBox{ 168.0f, 102.0f },
        capsule ? ColBox{ 273.0f, 65.0f } : ColBox{ 282.0f, 68.0f },
    };

    float yRow1 = showBars ? 3.5f : 4.5f;
    float yRow2 = showBars ? 18.0f : 20.5f;
    float rowH = 14.5f;
    float yCenter = (dim.heightDip - rowH) * 0.5f;
    float yBar = 34.5f;
    float barH = 2.2f;
    float sparkTop = std::min(13.5f, dim.heightDip * 0.35f);
    float sparkBot = std::max(sparkTop + 6.0f, dim.heightDip - 3.5f);

    wchar_t valBuf[48]{};

    // ---- Col 1: CPU ----
    {
        const auto& c = cols[0];
        D2D1_COLOR_F cpuCol = PickLoadColor(snap.cpuPercent, pal);
        if (showSpark) {
            DrawMicroSparkBg(rt, m_d2dFactory, D2D1::RectF(c.x, sparkTop, c.x + c.w, sparkBot),
                history.count, [&](size_t i) { return history.GetOrdered(history.cpu, i); },
                28.0, 100.0, cpuCol, light);
        }

        bool hasRow2 = (cfg.row2Col1 != Row2Col1Mode::Hide);
        float r1y = hasRow2 ? yRow1 : (showBars ? 9.5f : yCenter);

        DrawTextFormatted(rt, L"CPU", fmtLabel, D2D1::RectF(c.x, r1y, c.x + c.w, r1y + rowH), brLabel, false);
        _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1f%%", snap.cpuPercent);
        brVal->SetColor(cpuCol);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, r1y, c.x + c.w, r1y + rowH), brVal, true);

        if (cfg.row2Col1 == Row2Col1Mode::CpuFreq) {
            DrawTextFormatted(rt, L"主频", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brLabel, false);
            if (snap.hasCpuFreq) {
                _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.2fG", snap.cpuFreqGhz);
            } else {
                wcscpy_s(valBuf, L"--");
            }
            brVal->SetColor(pal.freq);
            DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brVal, true);
        } else if (cfg.row2Col1 == Row2Col1Mode::CpuTemp) {
            DrawTextFormatted(rt, L"温度", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brLabel, false);
            if (snap.hasCpuTemp) {
                _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f°C", snap.cpuTempC);
            } else {
                wcscpy_s(valBuf, L"--");
            }
            brVal->SetColor(snap.hasCpuTemp ? PickTempColor(snap.cpuTempC, pal) : pal.label);
            DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brVal, true);
        } else if (cfg.row2Col1 == Row2Col1Mode::MemPct) {
            DrawTextFormatted(rt, L"占存", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brLabel, false);
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1f%%", snap.ramPercent);
            brVal->SetColor(PickLoadColor(snap.ramPercent, pal, true));
            DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brVal, true);
        }

        if (showBars) {
            brVal->SetColor(cpuCol);
            DrawMiniBar(rt, c.x, yBar, c.w, barH, snap.cpuPercent, brTrack, brVal);
        }
    }

    // ---- Col 2: GPU ----
    {
        const auto& c = cols[1];
        D2D1_COLOR_F gpuCol = PickLoadColor(snap.gpuPercent, pal);
        if (showSpark && snap.hasGpuUsage) {
            DrawMicroSparkBg(rt, m_d2dFactory, D2D1::RectF(c.x, sparkTop, c.x + c.w, sparkBot),
                history.count, [&](size_t i) { return history.GetOrdered(history.gpu, i); },
                28.0, 100.0, gpuCol, light);
        }

        bool hasTemp = cfg.showGpuTemp && snap.hasGpuTemp;
        float r1y = hasTemp ? yRow1 : (showBars ? 9.5f : yCenter);

        DrawTextFormatted(rt, L"GPU", fmtLabel, D2D1::RectF(c.x, r1y, c.x + c.w, r1y + rowH), brLabel, false);
        if (snap.hasGpuUsage) {
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f%%", snap.gpuPercent);
        } else {
            wcscpy_s(valBuf, L"--");
        }
        brVal->SetColor(gpuCol);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, r1y, c.x + c.w, r1y + rowH), brVal, true);

        if (hasTemp) {
            DrawTextFormatted(rt, L"温度", fmtLabel, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brLabel, false);
            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.0f°C", snap.gpuTempC);
            brVal->SetColor(PickTempColor(snap.gpuTempC, pal));
            DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, yRow2, c.x + c.w, yRow2 + rowH), brVal, true);
        }

        if (showBars) {
            brVal->SetColor(gpuCol);
            DrawMiniBar(rt, c.x, yBar, c.w, barH, snap.hasGpuUsage ? snap.gpuPercent : 0.0, brTrack, brVal);
        }
    }

    // ---- Col 3: VRAM & RAM ----
    {
        const auto& c = cols[2];
        D2D1_COLOR_F vramCol = snap.hasVram ? PickLoadColor(snap.vramPercent, pal, true) : pal.label;
        D2D1_COLOR_F ramCol = PickLoadColor(snap.ramPercent, pal, true);

        auto drawDualMemRow = [&](float rowY, const wchar_t* label, bool hasVal, double usedGb, double pct, const D2D1_COLOR_F& col) {
            DrawTextFormatted(rt, label, fmtLabel, D2D1::RectF(c.x, rowY, c.x + c.w, rowY + rowH), brLabel, false);
            if (!hasVal) {
                brVal->SetColor(pal.label);
                DrawTextFormatted(rt, L"--", fmtValue, D2D1::RectF(c.x, rowY, c.x + c.w, rowY + rowH), brVal, true);
                return;
            }
            if (capsule) {
                _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1fGB", usedGb);
                brVal->SetColor(col);
                DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x, rowY, c.x + c.w, rowY + rowH), brVal, true);
                return;
            }
            wchar_t pctBuf[16]{};
            _snwprintf_s(pctBuf, _countof(pctBuf), _TRUNCATE, L"%.0f%%", pct);
            float pctW = MeasureTextWidth(pctBuf, fmtValue, true);

            _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.1fG", usedGb);
            float subRight = c.x + c.w - pctW - 4.5f;
            if (subRight > c.x + 26.0f) {
                DrawTextFormatted(rt, valBuf, m_fmtMinSub, D2D1::RectF(c.x + 24.0f, rowY + 0.3f, subRight, rowY + rowH + 0.3f), brLabel, true);
            }
            brVal->SetColor(col);
            DrawTextFormatted(rt, pctBuf, fmtValue, D2D1::RectF(c.x, rowY, c.x + c.w, rowY + rowH), brVal, true);
        };

        drawDualMemRow(yRow1, L"显存", snap.hasVram, snap.vramUsedGb, snap.vramPercent, vramCol);
        drawDualMemRow(yRow2, L"内存", true, snap.ramUsedGb, snap.ramPercent, ramCol);

        if (showBars) {
            float halfW = (c.w - 4.0f) * 0.5f;
            brVal->SetColor(vramCol);
            DrawMiniBar(rt, c.x, yBar, halfW, barH, snap.hasVram ? snap.vramPercent : 0.0, brTrack, brVal);
            brVal->SetColor(ramCol);
            DrawMiniBar(rt, c.x + halfW + 4.0f, yBar, halfW, barH, snap.ramPercent, brTrack, brVal);
        }
    }

    // ---- Col 4: Network Up & Down ----
    {
        const auto& c = cols[3];
        if (showSpark) {
            auto netNorm = [&](size_t i) -> float {
                double bps = std::max(history.GetOrdered(history.netDown, i), history.GetOrdered(history.netUp, i));
                if (bps <= 512.0) return 0.02f;
                return static_cast<float>(std::clamp(std::log10(bps / 512.0) / 5.3, 0.03, 1.0));
            };
            D2D1_COLOR_F netSparkCol = (snap.netUpBps > snap.netDownBps) ? pal.netUp : pal.netDown;
            DrawMicroSparkBg(rt, m_d2dFactory, D2D1::RectF(c.x, sparkTop, c.x + c.w, sparkBot),
                history.count, netNorm, 1.0, 1.0, netSparkCol, light, true);
        }

        float arrowX = c.x + 4.0f;

        bool upActive = snap.netUpBps >= 1024.0;
        brVal->SetColor(upActive ? pal.netUp : pal.netIdle);
        DrawVectorArrow(rt, m_d2dFactory, arrowX, yRow1 + rowH * 0.5f, true, brVal);
        FormatSpeedCompact(snap.netUpBps, valBuf, _countof(valBuf), false);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x + 10.0f, yRow1, c.x + c.w, yRow1 + rowH), brVal, true);

        bool downActive = snap.netDownBps >= 1024.0;
        brVal->SetColor(downActive ? pal.netDown : pal.netIdle);
        DrawVectorArrow(rt, m_d2dFactory, arrowX, yRow2 + rowH * 0.5f, false, brVal);
        FormatSpeedCompact(snap.netDownBps, valBuf, _countof(valBuf), false);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(c.x + 10.0f, yRow2, c.x + c.w, yRow2 + rowH), brVal, true);

        if (showBars) {
            double peakBps = std::max(snap.netDownBps, snap.netUpBps);
            double logPct = 0.0;
            if (peakBps > 512.0) {
                logPct = std::clamp((std::log10(peakBps / 512.0) / 5.3) * 100.0, 4.0, 100.0);
            }
            brVal->SetColor(snap.netDownBps >= snap.netUpBps ? pal.netDown : pal.netUp);
            DrawMiniBar(rt, c.x, yBar, c.w, barH, logPct, brTrack, brVal);
        }
    }

    // ---- Col 5: International gold, CNY per gram ----
    if (cfg.showGoldPrice) {
        float x = capsule ? 349.0f : 362.0f;
        float w = capsule ? 78.0f : 66.0f;
        const wchar_t* label = gold.cnyPerGram > 0.0 && !gold.fetchSucceeded ? L"金价·未更新" : L"金价·元/克";
        DrawTextFormatted(rt, label, fmtLabel, D2D1::RectF(x, yRow1, x + w, yRow1 + rowH), brLabel, false);
        if (gold.cnyPerGram > 0.0) _snwprintf_s(valBuf, _countof(valBuf), _TRUNCATE, L"%.2f", gold.cnyPerGram);
        else wcscpy_s(valBuf, L"--");
        brVal->SetColor(gold.fetchSucceeded ? (light ? Rgba(154, 101, 0) : Rgba(246, 201, 92)) : pal.label);
        DrawTextFormatted(rt, valBuf, fmtValue, D2D1::RectF(x, yRow2, x + w, yRow2 + rowH), brVal, true);
    }

    SafeRelease(brLabel);
    SafeRelease(brVal);
    SafeRelease(brTrack);
}

bool RenderEngine::RenderWidgetToWindow(
    HWND hwnd,
    POINT ptScreen,
    UINT dpi,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    const GoldQuote& gold,
    bool isHovered,
    bool isPressed) {
    WidgetDimensions dim = MeasureWidget(cfg);
    float scale = static_cast<float>(dpi) / 96.0f;
    int wPx = std::max(1, static_cast<int>(std::lround(dim.widthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(dim.heightDip * scale)));

    if (hwnd && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0) {
        return RenderWidgetViaDComp(hwnd, wPx, hPx, dpi, dim, cfg, snap, history, gold, isHovered, isPressed);
    }

    if (!EnsureWicTarget(m_widgetTarget, wPx, hPx, dpi)) return false;

    ID2D1RenderTarget* rt = m_widgetTarget.rt;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawWidgetContent(rt, dim, cfg, snap, history, gold, isHovered, isPressed);
    if (FAILED(rt->EndDraw())) return false;
    return hwnd ? PresentWicToWindow(hwnd, ptScreen, m_widgetTarget.bitmap, wPx, hPx) : true;
}
