#include "render/render_engine.hpp"
#include "render/palette.hpp"
#include "render/draw_primitives.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

bool RenderEngine::RenderFlyoutToWindow(
    HWND hwnd,
    POINT ptScreen,
    UINT dpi,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    int hoverActionIndex,
    RECT outActionRectsPx[4]) {
    const WidgetDimensions dim = FlyoutDimensions();
    float scale = static_cast<float>(dpi) / 96.0f;
    int wPx = std::max(1, static_cast<int>(std::lround(dim.widthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(dim.heightDip * scale)));

    if (!EnsureWicTarget(m_flyoutTarget, wPx, hPx, dpi)) return false;

    const bool light = ResolveEffectiveLightTheme(cfg);
    const Palette pal = MakePalette(light);

    ID2D1RenderTarget* rt = m_flyoutTarget.rt;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));

    ID2D1SolidColorBrush* brBg = nullptr;
    ID2D1SolidColorBrush* brBorder = nullptr;
    ID2D1SolidColorBrush* brCard = nullptr;
    ID2D1SolidColorBrush* brCardBorder = nullptr;
    ID2D1SolidColorBrush* brLabel = nullptr;
    ID2D1SolidColorBrush* brStrong = nullptr;
    ID2D1SolidColorBrush* brVal = nullptr;
    ID2D1SolidColorBrush* brTrack = nullptr;

    rt->CreateSolidColorBrush(pal.flyBg, &brBg);
    rt->CreateSolidColorBrush(pal.flyBorder, &brBorder);
    rt->CreateSolidColorBrush(pal.cardBg, &brCard);
    rt->CreateSolidColorBrush(pal.cardBorder, &brCardBorder);
    rt->CreateSolidColorBrush(pal.label, &brLabel);
    rt->CreateSolidColorBrush(pal.labelStrong, &brStrong);
    rt->CreateSolidColorBrush(pal.ok, &brVal);
    rt->CreateSolidColorBrush(pal.trackBg, &brTrack);

    // Soft layered drop shadow around inner 368x340 card
    constexpr float padLeft = 6.0f;
    constexpr float padTop = 5.0f;
    constexpr float innerW = 368.0f;
    constexpr float innerH = 340.0f;

    DrawCardShadow(rt, padLeft, padTop, innerW, innerH, 12.0f, light);

    rt->SetTransform(D2D1::Matrix3x2F::Translation(padLeft, padTop));

    // Outer rounded panel
    D2D1_ROUNDED_RECT outer = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, innerW - 0.5f, innerH - 0.5f), 12.0f, 12.0f);
    rt->FillRoundedRectangle(&outer, brBg);
    rt->DrawRoundedRectangle(&outer, brBorder, 1.0f);

    // Header
    DrawTextFormatted(rt, L"系统性能概览", m_fmtFlyTitle, D2D1::RectF(16.0f, 12.0f, 160.0f, 34.0f), brStrong, false);
    const wchar_t* gpuTitle = snap.gpuName.empty() ? L"集成/默认显卡" : snap.gpuName.c_str();
    m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    DrawTextFormatted(rt, gpuTitle, m_fmtFlySub, D2D1::RectF(130.0f, 14.0f, innerW - 16.0f, 32.0f), brLabel, false);
    m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 2x2 Cards
    float padX = 14.0f;
    float gap = 10.0f;
    float cardW = (innerW - padX * 2.0f - gap) * 0.5f;
    float cardH = 112.0f;
    float topY = 44.0f;

    auto drawCardBox = [&](float x, float y, float w, float h) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 9.0f, 9.0f);
        rt->FillRoundedRectangle(&rr, brCard);
        rt->DrawRoundedRectangle(&rr, brCardBorder, 1.0f);
    };

    wchar_t buf[64]{};

    // Card 1: CPU
    {
        float cx = padX, cy = topY;
        drawCardBox(cx, cy, cardW, cardH);
        DrawTextFormatted(rt, L"处理器 CPU", m_fmtFlyCardLabel, D2D1::RectF(cx + 12, cy + 8, cx + cardW - 12, cy + 24), brLabel, false);

        if (snap.hasCpuTemp) {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f°C", snap.cpuTempC);
            brVal->SetColor(PickTempColor(snap.cpuTempC, pal));
            DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 100, cy + 8, cx + cardW - 12, cy + 24), brVal, true);
        }

        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f%%", snap.cpuPercent);
        brVal->SetColor(PickLoadColor(snap.cpuPercent, pal));
        DrawTextFormatted(rt, buf, m_fmtFlyCardBig, D2D1::RectF(cx + 12, cy + 25, cx + 105, cy + 52), brVal, true);

        if (snap.hasCpuFreq) {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.2f GHz", snap.cpuFreqGhz);
            m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawTextFormatted(rt, buf, m_fmtFlySub, D2D1::RectF(cx + 95, cy + 31, cx + cardW - 12, cy + 48), brLabel, true);
            m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        DrawSparkline(rt, m_d2dFactory, D2D1::RectF(cx + 12, cy + 58, cx + cardW - 12, cy + cardH - 12),
            history.count, [&](size_t i) { return history.GetOrdered(history.cpu, i); },
            100.0, PickLoadColor(snap.cpuPercent, pal));
    }

    // Card 2: GPU
    {
        float cx = padX + cardW + gap, cy = topY;
        drawCardBox(cx, cy, cardW, cardH);
        DrawTextFormatted(rt, L"图形处理器 GPU", m_fmtFlyCardLabel, D2D1::RectF(cx + 12, cy + 8, cx + cardW - 12, cy + 24), brLabel, false);

        if (snap.hasGpuUsage) _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f%%", snap.gpuPercent);
        else wcscpy_s(buf, L"--");
        brVal->SetColor(PickLoadColor(snap.gpuPercent, pal));
        DrawTextFormatted(rt, buf, m_fmtFlyCardBig, D2D1::RectF(cx + 12, cy + 25, cx + 100, cy + 52), brVal, true);

        if (snap.hasGpuTemp) {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f °C", snap.gpuTempC);
            brVal->SetColor(PickTempColor(snap.gpuTempC, pal));
            m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawTextFormatted(rt, buf, m_fmtFlySub, D2D1::RectF(cx + 95, cy + 31, cx + cardW - 12, cy + 48), brVal, true);
            m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        DrawSparkline(rt, m_d2dFactory, D2D1::RectF(cx + 12, cy + 58, cx + cardW - 12, cy + cardH - 12),
            history.count, [&](size_t i) { return history.GetOrdered(history.gpu, i); },
            100.0, PickLoadColor(snap.gpuPercent, pal));
    }

    // Card 3: VRAM & RAM
    {
        float cx = padX, cy = topY + cardH + gap;
        drawCardBox(cx, cy, cardW, cardH);
        DrawTextFormatted(rt, L"显存与物理内存", m_fmtFlyCardLabel, D2D1::RectF(cx + 12, cy + 8, cx + cardW - 12, cy + 24), brLabel, false);

        // VRAM row
        DrawTextFormatted(rt, L"显存", m_fmtFlySub, D2D1::RectF(cx + 12, cy + 30, cx + 48, cy + 46), brLabel, false);
        if (snap.hasVram) {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f / %.1f GB", snap.vramUsedGb, snap.vramTotalGb);
            brVal->SetColor(PickLoadColor(snap.vramPercent, pal, true));
        } else {
            wcscpy_s(buf, L"不可用");
            brVal->SetColor(pal.label);
        }
        DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 46, cy + 30, cx + cardW - 12, cy + 46), brVal, true);
        DrawMiniBar(rt, cx + 12, cy + 50, cardW - 24, 5.0f, snap.hasVram ? snap.vramPercent : 0.0, brTrack, brVal);

        // RAM row
        DrawTextFormatted(rt, L"内存", m_fmtFlySub, D2D1::RectF(cx + 12, cy + 66, cx + 48, cy + 82), brLabel, false);
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f / %.1f GB", snap.ramUsedGb, snap.ramTotalGb);
        brVal->SetColor(PickLoadColor(snap.ramPercent, pal, true));
        DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 46, cy + 66, cx + cardW - 12, cy + 82), brVal, true);
        DrawMiniBar(rt, cx + 12, cy + 86, cardW - 24, 5.0f, snap.ramPercent, brTrack, brVal);
    }

    // Card 4: Network
    {
        float cx = padX + cardW + gap, cy = topY + cardH + gap;
        drawCardBox(cx, cy, cardW, cardH);
        const wchar_t* nicLabel = snap.nicName.empty() ? L"网络吞吐" : snap.nicName.c_str();
        DrawTextFormatted(rt, nicLabel, m_fmtFlyCardLabel, D2D1::RectF(cx + 12, cy + 8, cx + cardW - 12, cy + 24), brLabel, false);

        // Up/Down values
        brVal->SetColor(pal.netUp);
        DrawVectorArrow(rt, m_d2dFactory, cx + 16, cy + 36, true, brVal);
        FormatSpeedCompact(snap.netUpBps, buf, _countof(buf), true);
        DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 24, cy + 28, cx + cardW - 12, cy + 44), brVal, true);

        brVal->SetColor(pal.netDown);
        DrawVectorArrow(rt, m_d2dFactory, cx + 16, cy + 54, false, brVal);
        FormatSpeedCompact(snap.netDownBps, buf, _countof(buf), true);
        DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(cx + 24, cy + 46, cx + cardW - 12, cy + 62), brVal, true);

        double maxNet = 10240.0;
        for (size_t i = 0; i < history.count; ++i) {
            maxNet = std::max(maxNet, static_cast<double>(std::max(
                history.GetOrdered(history.netDown, i),
                history.GetOrdered(history.netUp, i))));
        }
        DrawSparkline(rt, m_d2dFactory, D2D1::RectF(cx + 12, cy + 68, cx + cardW - 12, cy + cardH - 12),
            history.count, [&](size_t i) { return history.GetOrdered(history.netUp, i); },
            maxNet, pal.netUp, true);
        DrawSparkline(rt, m_d2dFactory, D2D1::RectF(cx + 12, cy + 68, cx + cardW - 12, cy + cardH - 12),
            history.count, [&](size_t i) { return history.GetOrdered(history.netDown, i); },
            maxNet, pal.netDown, false);
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

    ID2D1SolidColorBrush* brBtn = nullptr;
    ID2D1SolidColorBrush* brBtnText = nullptr;
    rt->CreateSolidColorBrush(pal.btnBg, &brBtn);
    rt->CreateSolidColorBrush(pal.btnText, &brBtnText);

    for (int i = 0; i < 4; ++i) {
        float bx = padX + i * (btnW + btnGap);
        brBtn->SetColor(hoverActionIndex == i ? pal.btnHover : pal.btnBg);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(bx, btnY, bx + btnW, btnY + btnH), 6.0f, 6.0f);
        rt->FillRoundedRectangle(&rr, brBtn);
        DrawTextFormatted(rt, btnLabels[i], m_fmtFlyBtn, D2D1::RectF(bx + 2, btnY, bx + btnW - 2, btnY + btnH), brBtnText, false);

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
    m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    DrawTextFormatted(rt, sessionBuf, m_fmtFlySub, D2D1::RectF(padX, btnY + btnH + 4.0f, innerW - padX, innerH - 6.0f), brLabel, true);
    m_fmtFlySub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    rt->SetTransform(D2D1::Matrix3x2F::Identity());

    SafeRelease(brBtn);
    SafeRelease(brBtnText);
    SafeRelease(brBg);
    SafeRelease(brBorder);
    SafeRelease(brCard);
    SafeRelease(brCardBorder);
    SafeRelease(brLabel);
    SafeRelease(brStrong);
    SafeRelease(brVal);
    SafeRelease(brTrack);

    if (FAILED(rt->EndDraw())) return false;
    return PresentWicToWindow(hwnd, ptScreen, m_flyoutTarget.bitmap, wPx, hPx);
}
