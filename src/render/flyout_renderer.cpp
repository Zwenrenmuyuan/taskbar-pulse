#include "render/render_engine.hpp"
#include "render/palette.hpp"
#include "render/draw_primitives.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {

std::wstring ShortenGpuName(const std::wstring& full) {
    if (full.empty()) return L"集成显卡";
    std::wstring s = full;
    const wchar_t* prefixes[] = {
        L"NVIDIA GeForce ",
        L"NVIDIA ",
        L"AMD Radeon(TM) ",
        L"AMD Radeon ",
        L"Intel(R) ",
        L"Intel ",
    };
    for (const wchar_t* p : prefixes) {
        size_t len = wcslen(p);
        if (s.rfind(p, 0) == 0 && s.size() > len + 2) {
            s.erase(0, len);
            break;
        }
    }
    if (s.size() > 22) {
        s.resize(21);
        s.push_back(L'…');
    }
    return s;
}

std::wstring ShortenNicName(const std::wstring& full) {
    if (full.empty()) return L"网络";
    if (full.find(L"10Gb") != std::wstring::npos) return L"10GbE";
    if (full.find(L"2.5Gb") != std::wstring::npos) return L"2.5GbE";
    if (full.find(L"5Gb") != std::wstring::npos) return L"5GbE";
    if (full.find(L"GbE") != std::wstring::npos || full.find(L"Gigabit") != std::wstring::npos) return L"GbE";
    if (full.find(L"Wi-Fi") != std::wstring::npos || full.find(L"Wireless") != std::wstring::npos) return L"Wi-Fi";
    if (full.size() > 14) {
        return full.substr(0, 13) + L"…";
    }
    return full;
}

} // namespace

void RenderEngine::DrawFlyoutContent(
    ID2D1RenderTarget* rt,
    ID2D1SolidColorBrush* brMain,
    ID2D1SolidColorBrush* brAux,
    UINT dpi,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    const GoldQuote& gold,
    bool autoStartEnabled,
    const FlyoutDisplayOptions& displayOpts,
    const AnimatedMetrics& anim,
    const FlyoutAnimState& flyAnim,
    FlyoutControl hoverCtrl,
    FlyoutControl pressedCtrl,
    int sheetHoverIdx,
    FlyoutHitMap& outHitMap) {
    outHitMap = {};

    const float scale = static_cast<float>(dpi) / 96.0f;
    const bool light = ResolveEffectiveLightTheme(cfg);
    const Palette pal = MakePalette(light, false);
    const bool useAnimColors = anim.colorsInitialized && anim.lastLightTheme == light;

    constexpr float padX = 10.0f;
    constexpr float padTop = 7.0f;
    constexpr float innerW = 332.0f;
    constexpr float innerH = 364.0f;
    constexpr float cornerR = 14.0f;

    DrawCardShadow(rt, brMain, padX, padTop, innerW, innerH, cornerR, light);

    D2D1_MATRIX_3X2_F prevTransform{};
    rt->GetTransform(&prevTransform);
    D2D1_MATRIX_3X2_F panelBase = D2D1::Matrix3x2F::Translation(padX, padTop) * prevTransform;
    rt->SetTransform(panelBase);

    // Outer frosted zinc shell
    D2D1_ROUNDED_RECT outer = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, innerW - 0.5f, innerH - 0.5f), cornerR, cornerR);
    brMain->SetColor(pal.flyBg);
    rt->FillRoundedRectangle(&outer, brMain);
    brMain->SetColor(pal.flyBorder);
    rt->DrawRoundedRectangle(&outer, brMain, 1.0f);

    // Crisp top bevel highlight
    brMain->SetColor(pal.flyHighlight);
    rt->DrawLine(D2D1::Point2F(16.0f, 1.5f), D2D1::Point2F(innerW - 16.0f, 1.5f), brMain, 1.0f);

    const float slide = std::clamp(flyAnim.settingsSlide, 0.0f, 1.0f);
    const float sheetT = std::clamp(flyAnim.sheetOpen, 0.0f, 1.0f);
    const bool sheetInteractive = (slide >= 0.5f && sheetT >= 0.5f);
    const bool overviewInteractive = (slide < 0.5f);
    const bool settingsInteractive = (slide >= 0.5f && !sheetInteractive);
    const D2D1_RECT_F pageClip = D2D1::RectF(1.0f, 1.0f, innerW - 1.0f, innerH - 1.0f);

    auto toHitRect = [&](float pageOffX, float x1, float y1, float x2, float y2) -> RECT {
        x1 = std::clamp(x1 + pageOffX, pageClip.left, pageClip.right);
        x2 = std::clamp(x2 + pageOffX, pageClip.left, pageClip.right);
        y1 = std::clamp(y1, pageClip.top, pageClip.bottom);
        y2 = std::clamp(y2, pageClip.top, pageClip.bottom);
        return RECT{
            static_cast<LONG>(std::lround((padX + x1) * scale)),
            static_cast<LONG>(std::lround((padTop + y1) * scale)),
            static_cast<LONG>(std::lround((padX + x2) * scale)),
            static_cast<LONG>(std::lround((padTop + y2) * scale)),
        };
    };

    auto ctrlHoverT = [&](FlyoutControl c) -> float {
        int idx = static_cast<int>(c);
        return (idx >= 0 && idx < static_cast<int>(FlyoutControl::Count))
            ? std::clamp(flyAnim.ctrlHover[idx], 0.0f, 1.0f)
            : 0.0f;
    };
    auto ctrlPressT = [&](FlyoutControl c) -> float {
        int idx = static_cast<int>(c);
        return (idx >= 0 && idx < static_cast<int>(FlyoutControl::Count))
            ? std::clamp(flyAnim.ctrlPress[idx], 0.0f, 1.0f)
            : 0.0f;
    };

    // Clip sliding pages cleanly inside inner panel bounds
    rt->PushAxisAlignedClip(pageClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // =========================================================================
    // PAGE 0: B1 SWISS INSTRUMENT OVERVIEW
    // =========================================================================
    if (slide < 0.996f) {
        const float pageOffX = -slide * innerW;
        rt->SetTransform(D2D1::Matrix3x2F::Translation(pageOffX, 0.0f) * panelBase);

        // Header
        {
            D2D1_COLOR_F halo = pal.ok;
            halo.a = light ? 0.16f : 0.20f;
            brMain->SetColor(halo);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(18.5f, 22.0f), 5.8f, 5.8f), brMain);
            brMain->SetColor(pal.ok);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(18.5f, 22.0f), 3.1f, 3.1f), brMain);

            brMain->SetColor(pal.labelStrong);
            DrawTextFormatted(rt, L"系统概览", m_fmtFlyTitle, D2D1::RectF(29.0f, 10.0f, 190.0f, 34.0f), brMain, false);

            // Top-right gear button (26x26)
            const float gx1 = innerW - 38.0f, gy1 = 9.0f, gx2 = innerW - 12.0f, gy2 = 35.0f;
            float gh = ctrlHoverT(FlyoutControl::GearBtn);
            float gp = ctrlPressT(FlyoutControl::GearBtn);
            if (gh > 0.01f || gp > 0.01f) {
                D2D1_COLOR_F bg = pal.rowHover;
                bg.a *= std::max(gh, gp * 1.45f);
                float ins = gp * 0.6f;
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(gx1 + ins, gy1 + ins, gx2 - ins, gy2 - ins), 7.0f, 7.0f);
                brMain->SetColor(bg);
                rt->FillRoundedRectangle(&rr, brMain);
            }
            brMain->SetColor(anim::LerpColor(pal.label, pal.labelStrong, std::max(gh, gp)));
            DrawGearIcon(rt, m_roundStroke, (gx1 + gx2) * 0.5f, (gy1 + gy2) * 0.5f, brMain);

            if (overviewInteractive) {
                outHitMap.controls[static_cast<int>(FlyoutControl::GearBtn)] = toHitRect(pageOffX, gx1, gy1, gx2, gy2);
            }
        }

        auto drawRowWash = [&](int rowIdx, float y1, float y2) {
            float t = std::clamp(flyAnim.rowHover[rowIdx], 0.0f, 1.0f);
            if (t > 0.01f) {
                D2D1_COLOR_F c = pal.rowHover;
                c.a *= t;
                brMain->SetColor(c);
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(8.0f, y1, innerW - 8.0f, y2), 10.0f, 10.0f);
                rt->FillRoundedRectangle(&rr, brMain);
            }
            if (overviewInteractive) {
                outHitMap.overviewRows[rowIdx] = toHitRect(pageOffX, 8.0f, y1, innerW - 8.0f, y2);
            }
        };

        auto drawIconWell = [&](float x, float y) {
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + 28.0f, y + 28.0f), 8.0f, 8.0f);
            brMain->SetColor(pal.iconWell);
            rt->FillRoundedRectangle(&rr, brMain);
        };

        auto drawTempPill = [&](float x, float yCenter, double tempC) {
            wchar_t tbuf[24]{};
            _snwprintf_s(tbuf, _countof(tbuf), _TRUNCATE, L"%.0f°C", tempC);
            float tw = MeasureTextWidth(tbuf, m_fmtFlyBadge, true);
            float pw = std::max(33.0f, tw + 11.0f);
            float ph = 16.0f;
            float py = yCenter - ph * 0.5f;

            D2D1_COLOR_F bg{}, fg{};
            PickTempBadgeColors(tempC, pal, bg, fg);
            brMain->SetColor(bg);
            D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(D2D1::RectF(x, py, x + pw, py + ph), ph * 0.5f, ph * 0.5f);
            rt->FillRoundedRectangle(&pill, brMain);

            brMain->SetColor(fg);
            DrawTextFormatted(rt, tbuf, m_fmtFlyBadge, D2D1::RectF(x, py - 0.5f, x + pw, py + ph), brMain, true);
        };

        const double dispVramPct = anim.initialized ? anim.vramPct : (snap.hasVram ? snap.vramPercent : 0.0);
        const double dispRamPct = anim.initialized ? anim.ramPct : snap.ramPercent;
        const float slideOffset = anim.historySlide;

        wchar_t buf[64]{};

        // ---- Row 0: CPU ----
        {
            const float y1 = 44.0f, y2 = 104.0f;
            drawRowWash(0, y1, y2);
            drawIconWell(15.0f, 54.0f);
            brMain->SetColor(pal.label);
            DrawCpuIcon(rt, m_roundStroke, 29.0f, 68.0f, brMain);

            brAux->SetColor(pal.label);
            DrawTextFormatted(rt, L"CPU", m_fmtFlyCardLabel, D2D1::RectF(52.0f, 50.0f, 82.0f, 66.0f), brAux, false);
            if (snap.hasCpuTemp) {
                drawTempPill(82.0f, 58.0f, snap.cpuTempC);
            }

            D2D1_COLOR_F cpuTxtCol = useAnimColors ? anim.cpuCol : PickLoadColor(snap.cpuPercent, pal);
            D2D1_COLOR_F cpuSigCol = useAnimColors ? anim.cpuSignalCol : PickSignalColor(snap.cpuPercent, pal.accentCpu, pal);
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f", snap.cpuPercent);
            float bigW = MeasureTextWidth(buf, m_fmtFlyCardBig, true);
            brMain->SetColor(cpuTxtCol);
            DrawTextFormatted(rt, buf, m_fmtFlyCardBig, D2D1::RectF(52.0f, 67.0f, 52.0f + bigW + 3.0f, 96.0f), brMain, true);

            brAux->SetColor(pal.muted);
            DrawTextFormatted(rt, L"%", m_fmtFlyUnit, D2D1::RectF(52.0f + bigW + 1.5f, 74.5f, 52.0f + bigW + 16.0f, 94.0f), brAux, false);

            if (snap.hasCpuFreq) {
                _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.2f GHz", snap.cpuFreqGhz);
                float subLeft = 52.0f + bigW + 19.0f;
                if (subLeft < 196.0f) {
                    DrawTextFormatted(rt, buf, m_fmtFlyTiny, D2D1::RectF(subLeft, 76.0f, 199.0f, 93.0f), brAux, true);
                }
            }

            double cpuCeil = anim.initialized ? anim.cpuSparkCeil : 20.0;
            DrawMicroBarHistogram(rt, brMain,
                D2D1::RectF(204.0f, 57.0f, innerW - 16.0f, 92.0f),
                history.count, [&](size_t i) { return history.GetOrdered(history.cpu, i); },
                cpuCeil, cpuSigCol, slideOffset);
        }

        // ---- Row 1: GPU ----
        {
            const float y1 = 106.0f, y2 = 166.0f;
            drawRowWash(1, y1, y2);
            drawIconWell(15.0f, 116.0f);
            brMain->SetColor(pal.label);
            DrawGpuIcon(rt, m_roundStroke, 29.0f, 130.0f, brMain);

            brAux->SetColor(pal.label);
            DrawTextFormatted(rt, L"GPU", m_fmtFlyCardLabel, D2D1::RectF(52.0f, 112.0f, 82.0f, 128.0f), brAux, false);
            if (snap.hasGpuTemp) {
                drawTempPill(82.0f, 120.0f, snap.gpuTempC);
            }

            D2D1_COLOR_F gpuTxtCol = useAnimColors ? anim.gpuCol : PickLoadColor(snap.gpuPercent, pal);
            D2D1_COLOR_F gpuSigCol = useAnimColors ? anim.gpuSignalCol : PickSignalColor(snap.gpuPercent, pal.accentGpu, pal);
            float bigW = 24.0f;
            if (snap.hasGpuUsage) {
                if (snap.gpuPercent < 10.0) {
                    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f", snap.gpuPercent);
                } else {
                    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f", snap.gpuPercent);
                }
                bigW = MeasureTextWidth(buf, m_fmtFlyCardBig, true);
                brMain->SetColor(gpuTxtCol);
                DrawTextFormatted(rt, buf, m_fmtFlyCardBig, D2D1::RectF(52.0f, 129.0f, 52.0f + bigW + 3.0f, 158.0f), brMain, true);
                brAux->SetColor(pal.muted);
                DrawTextFormatted(rt, L"%", m_fmtFlyUnit, D2D1::RectF(52.0f + bigW + 1.5f, 136.5f, 52.0f + bigW + 16.0f, 156.0f), brAux, false);
            } else {
                brMain->SetColor(pal.muted);
                DrawTextFormatted(rt, L"--", m_fmtFlyCardBig, D2D1::RectF(52.0f, 129.0f, 92.0f, 158.0f), brMain, true);
            }

            if (snap.hasVram) {
                _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"显存 %.0f%%", snap.vramPercent);
                float subLeft = 52.0f + bigW + 19.0f;
                if (subLeft < 196.0f) {
                    brAux->SetColor(pal.muted);
                    DrawTextFormatted(rt, buf, m_fmtFlyTiny, D2D1::RectF(subLeft, 138.0f, 199.0f, 155.0f), brAux, true);
                }
            }

            double gpuCeil = anim.initialized ? anim.gpuSparkCeil : 20.0;
            DrawMicroBarHistogram(rt, brMain,
                D2D1::RectF(204.0f, 119.0f, innerW - 16.0f, 154.0f),
                history.count, [&](size_t i) { return history.GetOrdered(history.gpu, i); },
                gpuCeil, gpuSigCol, slideOffset);
        }

        // Hairline divider
        brMain->SetColor(pal.divider);
        rt->DrawLine(D2D1::Point2F(16.0f, 171.5f), D2D1::Point2F(innerW - 16.0f, 171.5f), brMain, 1.0f);

        // ---- Row 2: 24-Segment VRAM & RAM Precision Meters ----
        {
            const float y1 = 176.0f, y2 = 254.0f;
            drawRowWash(2, y1, y2);

            // VRAM
            brAux->SetColor(pal.label);
            DrawTextFormatted(rt, L"显存 VRAM", m_fmtFlyCardLabel, D2D1::RectF(16.0f, 183.0f, 124.0f, 199.0f), brAux, false);
            D2D1_COLOR_F vramTxtCol = snap.hasVram ? (useAnimColors ? anim.vramCol : PickLoadColor(snap.vramPercent, pal, true)) : pal.muted;
            D2D1_COLOR_F vramSigCol = snap.hasVram ? (useAnimColors ? anim.vramSignalCol : PickSignalColor(snap.vramPercent, pal.accentVram, pal, true)) : pal.muted;

            if (snap.hasVram) {
                _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f%%", snap.vramPercent);
                float pctW = MeasureTextWidth(buf, m_fmtValue, true);
                brMain->SetColor(vramTxtCol);
                DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(innerW - 16.0f - pctW - 4.0f, 183.0f, innerW - 16.0f, 199.0f), brMain, true);

                _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f / %.0f GB", snap.vramUsedGb, snap.vramTotalGb);
                brAux->SetColor(pal.muted);
                m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                DrawTextFormatted(rt, buf, m_fmtFlyTiny, D2D1::RectF(126.0f, 183.5f, innerW - 16.0f - pctW - 8.0f, 199.0f), brAux, true);
                m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            } else {
                brAux->SetColor(pal.muted);
                m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                DrawTextFormatted(rt, L"不可用", m_fmtFlyTiny, D2D1::RectF(160.0f, 183.5f, innerW - 16.0f, 199.0f), brAux, false);
                m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            DrawSegmentedMeter(rt, 16.0f, 203.0f, innerW - 32.0f, 6.5f, snap.hasVram ? dispVramPct : 0.0, 24, pal.trackBg, vramSigCol, brMain);

            // RAM
            brAux->SetColor(pal.label);
            DrawTextFormatted(rt, L"物理内存 RAM", m_fmtFlyCardLabel, D2D1::RectF(16.0f, 218.0f, 124.0f, 234.0f), brAux, false);
            D2D1_COLOR_F ramTxtCol = useAnimColors ? anim.ramCol : PickLoadColor(snap.ramPercent, pal, true);
            D2D1_COLOR_F ramSigCol = useAnimColors ? anim.ramSignalCol : PickSignalColor(snap.ramPercent, pal.accentRam, pal, true);

            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f%%", snap.ramPercent);
            float ramPctW = MeasureTextWidth(buf, m_fmtValue, true);
            brMain->SetColor(ramTxtCol);
            DrawTextFormatted(rt, buf, m_fmtValue, D2D1::RectF(innerW - 16.0f - ramPctW - 4.0f, 218.0f, innerW - 16.0f, 234.0f), brMain, true);

            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f / %.1f GB", snap.ramUsedGb, snap.ramTotalGb);
            brAux->SetColor(pal.muted);
            m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            DrawTextFormatted(rt, buf, m_fmtFlyTiny, D2D1::RectF(126.0f, 218.5f, innerW - 16.0f - ramPctW - 8.0f, 234.0f), brAux, true);
            m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

            DrawSegmentedMeter(rt, 16.0f, 238.0f, innerW - 32.0f, 6.5f, dispRamPct, 24, pal.trackBg, ramSigCol, brMain);
        }

        // Hairline divider
        brMain->SetColor(pal.divider);
        rt->DrawLine(D2D1::Point2F(16.0f, 259.5f), D2D1::Point2F(innerW - 16.0f, 259.5f), brMain, 1.0f);

        // ---- Row 3: Network ----
        {
            const float y1 = 264.0f, y2 = 326.0f;
            drawRowWash(3, y1, y2);
            drawIconWell(15.0f, 275.0f);
            brMain->SetColor(pal.label);
            DrawNetIcon(rt, m_roundStroke, 29.0f, 289.0f, brMain);

            brAux->SetColor(pal.label);
            DrawTextFormatted(rt, L"网络", m_fmtFlyCardLabel, D2D1::RectF(52.0f, 269.0f, 84.0f, 284.0f), brAux, false);

            wchar_t downTot[32]{};
            FormatBytesShort(snap.netSessionDownBytes, downTot, _countof(downTot));
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"累计 ↓%ls", downTot);
            brAux->SetColor(pal.muted);
            DrawTextFormatted(rt, buf, m_fmtFlyTiny, D2D1::RectF(83.0f, 269.5f, 198.0f, 284.0f), brAux, true);

            // Upload speed
            brMain->SetColor(pal.netUp);
            DrawVectorArrow(rt, m_roundStroke, 55.5f, 294.0f, true, brMain);
            FormatSpeedCompact(snap.netUpBps, buf, _countof(buf), true);
            brMain->SetColor(pal.labelStrong);
            DrawTextFormatted(rt, buf, m_fmtFlyCardLabel, D2D1::RectF(63.0f, 286.0f, 196.0f, 302.0f), brMain, true);

            // Download speed
            brMain->SetColor(pal.netDown);
            DrawVectorArrow(rt, m_roundStroke, 55.5f, 310.0f, false, brMain);
            FormatSpeedCompact(snap.netDownBps, buf, _countof(buf), true);
            brMain->SetColor(pal.labelStrong);
            DrawTextFormatted(rt, buf, m_fmtFlyCardLabel, D2D1::RectF(63.0f, 302.0f, 196.0f, 318.0f), brMain, true);

            double maxNet = anim.initialized ? std::max(10240.0, anim.netFlyoutMax) : 10240.0;
            if (!anim.initialized) {
                for (size_t i = 0; i < history.count; ++i) {
                    maxNet = std::max(maxNet, static_cast<double>(std::max(
                        history.GetOrdered(history.netDown, i),
                        history.GetOrdered(history.netUp, i))));
                }
            }
            D2D1_RECT_F netRect = D2D1::RectF(204.0f, 275.0f, innerW - 16.0f, 315.0f);
            DrawSparkline(rt, m_d2dFactory, m_roundStroke, brMain,
                netRect, history.count, [&](size_t i) { return history.GetOrdered(history.netUp, i); },
                maxNet, pal.netUp, true, slideOffset, false);
            DrawSparkline(rt, m_d2dFactory, m_roundStroke, brMain,
                netRect, history.count, [&](size_t i) { return history.GetOrdered(history.netDown, i); },
                maxNet, pal.netDown, false, slideOffset, true);
        }

        // ---- Quiet Hardware + Gold Footer ----
        {
            brMain->SetColor(pal.divider);
            rt->DrawLine(D2D1::Point2F(16.0f, 331.5f), D2D1::Point2F(innerW - 16.0f, 331.5f), brMain, 1.0f);

            std::wstring hwLine = ShortenGpuName(snap.gpuName) + L" · " + ShortenNicName(snap.nicName);
            brAux->SetColor(pal.muted);
            DrawTextFormatted(rt, hwLine.c_str(), m_fmtFlyTiny, D2D1::RectF(16.0f, 335.0f, 212.0f, 358.0f), brAux, false);

            if (cfg.showGoldPrice) {
                if (gold.cnyPerGram > 0.0) {
                    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"金价 ¥%.2f/g", gold.cnyPerGram);
                } else {
                    wcscpy_s(buf, L"金价 --");
                }
                brMain->SetColor(gold.fetchSucceeded ? pal.labelStrong : pal.muted);
                m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                DrawTextFormatted(rt, buf, m_fmtFlyTiny, D2D1::RectF(200.0f, 335.0f, innerW - 16.0f, 358.0f), brMain, true);
                m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            } else {
                wchar_t upTot[32]{};
                FormatBytesShort(snap.netSessionUpBytes, upTot, _countof(upTot));
                _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"上传 %ls", upTot);
                m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                DrawTextFormatted(rt, buf, m_fmtFlyTiny, D2D1::RectF(214.0f, 335.0f, innerW - 16.0f, 358.0f), brAux, true);
                m_fmtFlyTiny->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }
    }

    // =========================================================================
    // PAGE 1: SELF-CONTAINED IN-PLACE SETTINGS VIEW (ZERO 3RD-LEVEL MENU!)
    // =========================================================================
    if (slide > 0.004f) {
        const float pageOffX = (1.0f - slide) * innerW;
        rt->SetTransform(D2D1::Matrix3x2F::Translation(pageOffX, 0.0f) * panelBase);

        // Settings Header
        {
            const float bx1 = 12.0f, by1 = 7.0f, bx2 = 38.0f, by2 = 33.0f;
            float bh = ctrlHoverT(FlyoutControl::BackBtn);
            float bp = ctrlPressT(FlyoutControl::BackBtn);
            if (bh > 0.01f || bp > 0.01f) {
                D2D1_COLOR_F bg = pal.rowHover;
                bg.a *= std::max(bh, bp * 1.45f);
                float ins = bp * 0.6f;
                D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(bx1 + ins, by1 + ins, bx2 - ins, by2 - ins), 7.0f, 7.0f);
                brMain->SetColor(bg);
                rt->FillRoundedRectangle(&rr, brMain);
            }
            brMain->SetColor(anim::LerpColor(pal.label, pal.labelStrong, std::max(bh, bp)));
            DrawBackChevronIcon(rt, m_roundStroke, (bx1 + bx2) * 0.5f, (by1 + by2) * 0.5f, brMain);
            if (settingsInteractive) {
                outHitMap.controls[static_cast<int>(FlyoutControl::BackBtn)] = toHitRect(pageOffX, bx1, by1, bx2, by2);
            }

            brMain->SetColor(pal.labelStrong);
            DrawTextFormatted(rt, L"偏好设置", m_fmtFlyTitle, D2D1::RectF(42.0f, 8.0f, 200.0f, 32.0f), brMain, false);
        }

        constexpr float kRowH = 31.0f;

        // Helper for Segmented Control Row
        auto drawSegGroup = [&](float y, const wchar_t* label, const wchar_t* const* opts,
                                const FlyoutControl* ctrls, int count, int activeIdx, float segW) {
            brMain->SetColor(pal.labelStrong);
            DrawTextFormatted(rt, label, m_fmtFlySub, D2D1::RectF(22.0f, y, 134.0f, y + kRowH), brMain, false);

            const float sx2 = innerW - 20.0f;
            const float sx1 = sx2 - segW;
            const float sy1 = y + 4.0f;
            const float sy2 = y + kRowH - 4.0f;
            D2D1_ROUNDED_RECT track = D2D1::RoundedRect(D2D1::RectF(sx1, sy1, sx2, sy2), 6.5f, 6.5f);
            brMain->SetColor(pal.segBg);
            rt->FillRoundedRectangle(&track, brMain);

            const float pad = 2.0f;
            const float optW = (segW - pad * 2.0f) / static_cast<float>(count);
            for (int i = 0; i < count; ++i) {
                float ox1 = sx1 + pad + static_cast<float>(i) * optW;
                float ox2 = ox1 + optW;
                float oy1 = sy1 + pad;
                float oy2 = sy2 - pad;
                bool active = (i == activeIdx);
                float hT = ctrlHoverT(ctrls[i]);
                float pT = ctrlPressT(ctrls[i]);

                if (active) {
                    float ins = pT * 0.4f;
                    D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(D2D1::RectF(ox1 + ins, oy1 + ins, ox2 - ins, oy2 - ins), 5.0f, 5.0f);
                    brMain->SetColor(pal.segActive);
                    rt->FillRoundedRectangle(&pill, brMain);
                    brMain->SetColor(pal.segActiveBorder);
                    rt->DrawRoundedRectangle(&pill, brMain, 0.75f);
                } else if (hT > 0.01f) {
                    D2D1_COLOR_F hc = pal.rowHover;
                    hc.a *= hT;
                    D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(D2D1::RectF(ox1, oy1, ox2, oy2), 5.0f, 5.0f);
                    brMain->SetColor(hc);
                    rt->FillRoundedRectangle(&pill, brMain);
                }

                brAux->SetColor(active ? pal.labelStrong : anim::LerpColor(pal.label, pal.labelStrong, hT * 0.7f));
                DrawTextFormatted(rt, opts[i], m_fmtFlyBtn, D2D1::RectF(ox1, oy1, ox2, oy2), brAux, true);

                if (settingsInteractive) {
                    outHitMap.controls[static_cast<int>(ctrls[i])] = toHitRect(pageOffX, ox1, sy1, ox2, sy2);
                }
            }
        };

        // Group 1: Appearance, Rate, CPU Sub-row & Display Position (5 rows x 31 = 155 dip)
        {
            const float g1Top = 40.0f;
            const float g1Bot = g1Top + kRowH * 5.0f; // 195.0f
            D2D1_ROUNDED_RECT grp = D2D1::RoundedRect(D2D1::RectF(12.0f, g1Top, innerW - 12.0f, g1Bot), 10.0f, 10.0f);
            brMain->SetColor(pal.wellBg);
            rt->FillRoundedRectangle(&grp, brMain);

            const wchar_t* styleOpts[2] = { L"无框", L"胶囊" };
            const FlyoutControl styleCtrls[2] = { FlyoutControl::SegStyleMinimal, FlyoutControl::SegStyleCapsule };
            drawSegGroup(g1Top, L"任务栏风格", styleOpts, styleCtrls, 2, (cfg.style == VisualStyle::Minimal) ? 0 : 1, 104.0f);

            brMain->SetColor(pal.divider);
            rt->DrawLine(D2D1::Point2F(22.0f, g1Top + kRowH), D2D1::Point2F(innerW - 22.0f, g1Top + kRowH), brMain, 1.0f);

            const wchar_t* themeOpts[3] = { L"自动", L"浅色", L"深色" };
            const FlyoutControl themeCtrls[3] = { FlyoutControl::SegThemeAuto, FlyoutControl::SegThemeLight, FlyoutControl::SegThemeDark };
            int themeIdx = (cfg.theme == ThemeMode::Light) ? 1 : (cfg.theme == ThemeMode::Dark ? 2 : 0);
            drawSegGroup(g1Top + kRowH, L"外观主题", themeOpts, themeCtrls, 3, themeIdx, 138.0f);

            brMain->SetColor(pal.divider);
            rt->DrawLine(D2D1::Point2F(22.0f, g1Top + kRowH * 2.0f), D2D1::Point2F(innerW - 22.0f, g1Top + kRowH * 2.0f), brMain, 1.0f);

            const wchar_t* rateOpts[3] = { L"0.5s", L"1s", L"2s" };
            const FlyoutControl rateCtrls[3] = { FlyoutControl::SegRate500, FlyoutControl::SegRate1000, FlyoutControl::SegRate2000 };
            int rateIdx = (cfg.refreshMs <= 600) ? 0 : (cfg.refreshMs >= 1600 ? 2 : 1);
            drawSegGroup(g1Top + kRowH * 2.0f, L"刷新频率", rateOpts, rateCtrls, 3, rateIdx, 138.0f);

            brMain->SetColor(pal.divider);
            rt->DrawLine(D2D1::Point2F(22.0f, g1Top + kRowH * 3.0f), D2D1::Point2F(innerW - 22.0f, g1Top + kRowH * 3.0f), brMain, 1.0f);

            const wchar_t* row2Opts[4] = { L"温度", L"主频", L"内存", L"隐藏" };
            const FlyoutControl row2Ctrls[4] = {
                FlyoutControl::SegRow2Temp,
                FlyoutControl::SegRow2Freq,
                FlyoutControl::SegRow2Mem,
                FlyoutControl::SegRow2Hide,
            };
            int row2Idx = 1;
            if (cfg.row2Col1 == Row2Col1Mode::CpuTemp) row2Idx = 0;
            else if (cfg.row2Col1 == Row2Col1Mode::CpuFreq) row2Idx = 1;
            else if (cfg.row2Col1 == Row2Col1Mode::MemPct) row2Idx = 2;
            else if (cfg.row2Col1 == Row2Col1Mode::Hide) row2Idx = 3;
            drawSegGroup(g1Top + kRowH * 3.0f, L"CPU 副行", row2Opts, row2Ctrls, 4, row2Idx, 174.0f);

            // Row 4: Display Position Segmented Control (unified grammar with rows 0-3)
            const float dispY = g1Top + kRowH * 4.0f;
            brMain->SetColor(pal.divider);
            rt->DrawLine(D2D1::Point2F(22.0f, dispY), D2D1::Point2F(innerW - 22.0f, dispY), brMain, 1.0f);

            const int dCount = std::clamp(displayOpts.count, 2, 4);
            const wchar_t* dispLabels[4] = {
                displayOpts.labels[0].c_str(),
                displayOpts.labels[1].c_str(),
                displayOpts.labels[2].c_str(),
                displayOpts.labels[3].c_str(),
            };
            const FlyoutControl dispCtrls[4] = {
                FlyoutControl::SegDisp0,
                FlyoutControl::SegDisp1,
                FlyoutControl::SegDisp2,
                FlyoutControl::SegDisp3,
            };
            const float dispSegW = displayOpts.usesSheet
                ? 150.0f
                : ((dCount == 2) ? 104.0f : ((dCount == 3) ? 138.0f : 174.0f));
            drawSegGroup(dispY, L"显示位置", dispLabels, dispCtrls, dCount, displayOpts.activeIndex, dispSegW);
        }

        // Group 2: Toggle Switches (4 rows x 31 = 124 dip)
        {
            const float g2Top = 203.0f;
            const float g2Bot = g2Top + kRowH * 4.0f; // 327.0f
            D2D1_ROUNDED_RECT grp = D2D1::RoundedRect(D2D1::RectF(12.0f, g2Top, innerW - 12.0f, g2Bot), 10.0f, 10.0f);
            brMain->SetColor(pal.wellBg);
            rt->FillRoundedRectangle(&grp, brMain);

            struct ToggleRowSpec {
                const wchar_t* label;
                FlyoutControl ctrl;
                bool on;
                int animIdx;
            };
            const ToggleRowSpec rows[4] = {
                { L"任务栏底部状态微条", FlyoutControl::ToggleMiniBars, cfg.showMiniBars, 0 },
                { L"显示 GPU 温度", FlyoutControl::ToggleGpuTemp, cfg.showGpuTemp, 1 },
                { L"显示国际金价（元/克）", FlyoutControl::ToggleGold, cfg.showGoldPrice, 2 },
                { L"开机自动启动", FlyoutControl::ToggleAutoStart, autoStartEnabled, 3 },
            };

            const D2D1_COLOR_F offThumb = light ? Rgba(110, 113, 124, 1.0f) : Rgba(168, 172, 182, 1.0f);
            const D2D1_COLOR_F onThumb = Rgba(255, 255, 255, 1.0f);

            for (int i = 0; i < 4; ++i) {
                float ry = g2Top + static_cast<float>(i) * kRowH;
                if (i > 0) {
                    brMain->SetColor(pal.divider);
                    rt->DrawLine(D2D1::Point2F(22.0f, ry), D2D1::Point2F(innerW - 22.0f, ry), brMain, 1.0f);
                }

                float hT = ctrlHoverT(rows[i].ctrl);
                if (hT > 0.01f) {
                    D2D1_COLOR_F hc = pal.rowHover;
                    hc.a *= hT;
                    brMain->SetColor(hc);
                    D2D1_ROUNDED_RECT hr = D2D1::RoundedRect(D2D1::RectF(15.0f, ry + 2.0f, innerW - 15.0f, ry + kRowH - 2.0f), 7.0f, 7.0f);
                    rt->FillRoundedRectangle(&hr, brMain);
                }

                brMain->SetColor(pal.labelStrong);
                DrawTextFormatted(rt, rows[i].label, m_fmtFlySub, D2D1::RectF(22.0f, ry, 258.0f, ry + kRowH), brMain, false);

                float prog = flyAnim.togglesInitialized
                    ? std::clamp(flyAnim.togglePos[rows[i].animIdx], 0.0f, 1.0f)
                    : (rows[i].on ? 1.0f : 0.0f);
                DrawToggleSwitch(rt, innerW - 54.0f, ry + (kRowH - 18.0f) * 0.5f, 34.0f, 18.0f, prog,
                    pal.trackBg, pal.accentCpu, offThumb, onThumb, brMain);

                if (settingsInteractive) {
                    outHitMap.controls[static_cast<int>(rows[i].ctrl)] = toHitRect(pageOffX, 14.0f, ry, innerW - 14.0f, ry + kRowH);
                }
            }
        }

        // Clean Footer (Hint + Reset Position + Exit — NO redundant 3rd-level menu!)
        {
            auto drawFooterBtn = [&](FlyoutControl c, float x1, float x2, const wchar_t* label, bool danger) {
                const float y1 = 333.0f, y2 = 357.0f;
                float hT = ctrlHoverT(c);
                float pT = ctrlPressT(c);
                if (hT > 0.01f || pT > 0.01f) {
                    D2D1_COLOR_F bg = danger ? pal.badgeHotBg : pal.rowHover;
                    bg.a *= std::max(hT, pT * 1.4f);
                    float ins = pT * 0.5f;
                    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x1 + ins, y1 + ins, x2 - ins, y2 - ins), 6.0f, 6.0f);
                    brMain->SetColor(bg);
                    rt->FillRoundedRectangle(&rr, brMain);
                }
                D2D1_COLOR_F txt = danger
                    ? anim::LerpColor(pal.label, pal.badgeHotFg, std::max(hT, pT))
                    : anim::LerpColor(pal.label, pal.labelStrong, std::max(hT, pT));
                brAux->SetColor(txt);
                DrawTextFormatted(rt, label, m_fmtFlyBtn, D2D1::RectF(x1, y1, x2, y2), brAux, false);
                if (settingsInteractive) {
                    outHitMap.controls[static_cast<int>(c)] = toHitRect(pageOffX, x1, y1, x2, y2);
                }
            };

            brAux->SetColor(pal.muted);
            DrawTextFormatted(rt, L"Alt+拖动任务栏可微调位置", m_fmtFlyTiny, D2D1::RectF(16.0f, 333.0f, 200.0f, 357.0f), brAux, false);

            drawFooterBtn(FlyoutControl::BtnResetPos, 210.0f, 270.0f, L"重置位置", false);
            drawFooterBtn(FlyoutControl::BtnExit, 274.0f, 320.0f, L"退出", true);
        }

        // ---- Bottom Modal Sheet for 4+ Monitor Picker (stays inside Settings context!) ----
        if (sheetT > 0.004f && !displayOpts.items.empty()) {
            // Dimmed backdrop scrim inside rounded panel
            D2D1_COLOR_F scrimCol = Rgba(12, 14, 20, (light ? 0.24f : 0.48f) * sheetT);
            brMain->SetColor(scrimCol);
            D2D1_ROUNDED_RECT scrimRr = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, innerW - 0.5f, innerH - 0.5f), cornerR, cornerR);
            rt->FillRoundedRectangle(&scrimRr, brMain);

            const float sx1 = 6.0f;
            const float sx2 = innerW - 6.0f;
            constexpr float kItemH = 38.0f;
            constexpr float kListPadY = 5.0f;
            constexpr float kHeaderH = 40.0f;
            constexpr float kMaxListH = 228.0f; // 6 crisp rows visible at once, wheel-scrollable beyond

            const float contentH = static_cast<float>(displayOpts.items.size()) * kItemH + kListPadY * 2.0f;
            const float listH = std::min(contentH, kMaxListH);
            const float sheetH = kHeaderH + listH + 4.0f;
            const float maxScroll = std::max(0.0f, contentH - listH);
            outHitMap.sheetMaxScrollDip = maxScroll;

            const float targetTop = innerH - sheetH - 6.0f;
            const float hiddenTop = innerH + 10.0f;
            const float sy1 = hiddenTop + (targetTop - hiddenTop) * sheetT;
            const float sy2 = sy1 + sheetH;

            // Soft upward elevation shadow under sheet
            for (int s = 3; s >= 1; --s) {
                float ex = static_cast<float>(s) * 1.6f;
                brMain->SetColor(Rgba(8, 12, 22, (light ? 0.045f : 0.14f) * sheetT * (1.0f - static_cast<float>(s - 1) * 0.28f)));
                D2D1_ROUNDED_RECT sh = D2D1::RoundedRect(D2D1::RectF(sx1 - ex * 0.4f, sy1 - ex, sx2 + ex * 0.4f, sy2), 12.0f + ex * 0.3f, 12.0f + ex * 0.3f);
                rt->FillRoundedRectangle(&sh, brMain);
            }

            // Sheet surface (opaque so settings rows beneath never bleed through)
            D2D1_COLOR_F sheetBg = light ? Rgba(253, 253, 255, 1.0f) : Rgba(33, 35, 41, 1.0f);
            D2D1_ROUNDED_RECT sheetRr = D2D1::RoundedRect(D2D1::RectF(sx1, sy1, sx2, sy2), 12.0f, 12.0f);
            brMain->SetColor(sheetBg);
            rt->FillRoundedRectangle(&sheetRr, brMain);
            brMain->SetColor(pal.flyBorder);
            rt->DrawRoundedRectangle(&sheetRr, brMain, 1.0f);

            // Top grab handle
            brMain->SetColor(pal.trackBg);
            D2D1_ROUNDED_RECT handle = D2D1::RoundedRect(
                D2D1::RectF(innerW * 0.5f - 16.0f, sy1 + 6.5f, innerW * 0.5f + 16.0f, sy1 + 10.0f), 1.75f, 1.75f);
            rt->FillRoundedRectangle(&handle, brMain);

            // Header title + count + close button
            wchar_t hdrBuf[64]{};
            _snwprintf_s(hdrBuf, _countof(hdrBuf), _TRUNCATE, L"选择指定显示器 (%u 块)",
                static_cast<unsigned>(displayOpts.items.size()));
            brMain->SetColor(pal.labelStrong);
            DrawTextFormatted(rt, hdrBuf, m_fmtFlyCardLabel,
                D2D1::RectF(sx1 + 12.0f, sy1 + 14.0f, sx2 - 36.0f, sy1 + 36.0f), brMain, true);

            const float cx1 = sx2 - 30.0f, cy1 = sy1 + 14.0f, cx2 = sx2 - 8.0f, cy2 = sy1 + 36.0f;
            float ch = ctrlHoverT(FlyoutControl::SheetCloseBtn);
            float cp = ctrlPressT(FlyoutControl::SheetCloseBtn);
            if (ch > 0.01f || cp > 0.01f) {
                D2D1_COLOR_F bg = pal.rowHover;
                bg.a *= std::max(ch, cp * 1.45f);
                D2D1_ROUNDED_RECT crr = D2D1::RoundedRect(D2D1::RectF(cx1, cy1, cx2, cy2), 6.0f, 6.0f);
                brMain->SetColor(bg);
                rt->FillRoundedRectangle(&crr, brMain);
            }
            brMain->SetColor(anim::LerpColor(pal.label, pal.labelStrong, std::max(ch, cp)));
            DrawCloseXIcon(rt, m_roundStroke, (cx1 + cx2) * 0.5f, (cy1 + cy2) * 0.5f, brMain);

            brMain->SetColor(pal.divider);
            rt->DrawLine(D2D1::Point2F(sx1 + 10.0f, sy1 + kHeaderH - 0.5f), D2D1::Point2F(sx2 - 10.0f, sy1 + kHeaderH - 0.5f), brMain, 1.0f);

            const float listTop = sy1 + kHeaderH;
            const float listBot = listTop + listH;
            if (sheetInteractive) {
                outHitMap.sheetCard = toHitRect(pageOffX, sx1, sy1, sx2, sy2);
                outHitMap.sheetListViewport = toHitRect(pageOffX, sx1 + 2.0f, listTop, sx2 - 2.0f, listBot);
                outHitMap.controls[static_cast<int>(FlyoutControl::SheetCloseBtn)] = toHitRect(pageOffX, cx1, cy1, cx2, cy2);
                outHitMap.sheetItemRects.assign(displayOpts.items.size(), RECT{});
            }

            // Clipped scrollable monitor list
            rt->PushAxisAlignedClip(D2D1::RectF(sx1 + 1.0f, listTop, sx2 - 1.0f, listBot), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            const float scrollY = std::clamp(flyAnim.sheetScrollY, 0.0f, maxScroll);
            const bool hasScroll = (maxScroll > 0.5f);
            const float rowRight = sx2 - (hasScroll ? 12.0f : 6.0f);

            D2D1_COLOR_F selBg = pal.accentCpu;
            selBg.a = light ? 0.11f : 0.15f;

            for (size_t i = 0; i < displayOpts.items.size(); ++i) {
                const auto& item = displayOpts.items[i];
                const float ry = listTop + kListPadY + static_cast<float>(i) * kItemH - scrollY;
                if (ry + kItemH < listTop - 2.0f || ry > listBot + 2.0f) continue;

                const bool hovered = (static_cast<int>(i) == sheetHoverIdx);
                if (item.selected || hovered) {
                    brMain->SetColor(item.selected ? selBg : pal.rowHover);
                    D2D1_ROUNDED_RECT rrr = D2D1::RoundedRect(
                        D2D1::RectF(sx1 + 6.0f, ry + 1.5f, rowRight, ry + kItemH - 1.5f), 8.0f, 8.0f);
                    rt->FillRoundedRectangle(&rrr, brMain);
                }

                // Number badge (22x22)
                const float nx1 = sx1 + 13.0f;
                const float ny1 = ry + (kItemH - 22.0f) * 0.5f;
                D2D1_ROUNDED_RECT numRr = D2D1::RoundedRect(D2D1::RectF(nx1, ny1, nx1 + 22.0f, ny1 + 22.0f), 6.0f, 6.0f);
                brMain->SetColor(item.selected ? pal.accentCpu : pal.segBg);
                rt->FillRoundedRectangle(&numRr, brMain);

                wchar_t numBuf[12]{};
                _snwprintf_s(numBuf, _countof(numBuf), _TRUNCATE, L"%u", static_cast<unsigned>(i + 1));
                D2D1_COLOR_F selNumFg = light ? Rgba(255, 255, 255, 1.0f) : Rgba(16, 20, 22, 1.0f);
                brAux->SetColor(item.selected ? selNumFg : pal.label);
                DrawTextFormatted(rt, numBuf, m_fmtFlyBadge, D2D1::RectF(nx1, ny1, nx1 + 22.0f, ny1 + 22.0f), brAux, true);

                // Badge pill width on line 1
                const float textLeft = nx1 + 30.0f;
                const float checkReserve = item.selected ? 24.0f : 8.0f;
                const float contentRight = rowRight - checkReserve;
                float badgeW = 0.0f;
                if (!item.badge.empty()) {
                    badgeW = std::max(28.0f, MeasureTextWidth(item.badge.c_str(), m_fmtFlyBadge, true) + 10.0f);
                }
                float titleW = MeasureTextWidth(item.title.c_str(), m_fmtFlySub, false);
                float maxTitleW = std::max(40.0f, contentRight - textLeft - (badgeW > 0.0f ? (badgeW + 6.0f) : 0.0f));
                float drawTitleW = std::min(titleW + 2.0f, maxTitleW);

                brMain->SetColor(pal.labelStrong);
                DrawTextFormatted(rt, item.title.c_str(), m_fmtFlySub,
                    D2D1::RectF(textLeft, ry + 4.0f, textLeft + drawTitleW, ry + 20.0f), brMain, false);

                if (badgeW > 0.0f) {
                    float bx1 = textLeft + drawTitleW + 5.0f;
                    float by1 = ry + 5.0f;
                    D2D1_ROUNDED_RECT bRr = D2D1::RoundedRect(D2D1::RectF(bx1, by1, bx1 + badgeW, by1 + 14.0f), 4.0f, 4.0f);
                    brMain->SetColor(pal.segBg);
                    rt->FillRoundedRectangle(&bRr, brMain);
                    brAux->SetColor(pal.label);
                    DrawTextFormatted(rt, item.badge.c_str(), m_fmtFlyBadge,
                        D2D1::RectF(bx1, by1 - 0.5f, bx1 + badgeW, by1 + 14.0f), brAux, true);
                }

                // Line 2: Resolution + DPI
                if (!item.subtitle.empty()) {
                    brAux->SetColor(pal.muted);
                    DrawTextFormatted(rt, item.subtitle.c_str(), m_fmtFlyTiny,
                        D2D1::RectF(textLeft, ry + 19.5f, contentRight, ry + 34.0f), brAux, true);
                }

                // Right checkmark when selected
                if (item.selected) {
                    brMain->SetColor(pal.accentCpu);
                    DrawMenuCheckIcon(rt, m_roundStroke, rowRight - 13.0f, ry + kItemH * 0.5f, brMain);
                }

                if (sheetInteractive) {
                    float hitTop = std::max(ry + 1.0f, listTop);
                    float hitBot = std::min(ry + kItemH - 1.0f, listBot);
                    if (hitBot > hitTop + 2.0f) {
                        outHitMap.sheetItemRects[i] = toHitRect(pageOffX, sx1 + 6.0f, hitTop, rowRight, hitBot);
                    }
                }
            }

            // Floating thin scrollbar thumb
            if (hasScroll) {
                const float sbPad = 5.0f;
                const float sbTrackH = std::max(20.0f, listH - sbPad * 2.0f);
                const float thumbH = std::clamp(sbTrackH * (listH / contentH), 22.0f, sbTrackH);
                const float thumbY = listTop + sbPad + (sbTrackH - thumbH) * (scrollY / maxScroll);
                D2D1_COLOR_F sbCol = pal.muted;
                sbCol.a = light ? 0.36f : 0.42f;
                brMain->SetColor(sbCol);
                D2D1_ROUNDED_RECT thumb = D2D1::RoundedRect(
                    D2D1::RectF(sx2 - 7.5f, thumbY, sx2 - 4.0f, thumbY + thumbH), 1.75f, 1.75f);
                rt->FillRoundedRectangle(&thumb, brMain);
            }

            rt->PopAxisAlignedClip();
        }
    }

    (void)hoverCtrl;
    (void)pressedCtrl;

    rt->PopAxisAlignedClip();
    rt->SetTransform(prevTransform);
}

bool RenderEngine::RenderFlyoutToWindow(
    HWND hwnd,
    POINT ptScreen,
    UINT dpi,
    const AppConfig& cfg,
    const SystemSnapshot& snap,
    const HistoryBuffer& history,
    const GoldQuote& gold,
    bool autoStartEnabled,
    const FlyoutDisplayOptions& displayOpts,
    const AnimatedMetrics& anim,
    const FlyoutAnimState& flyAnim,
    FlyoutControl hoverCtrl,
    FlyoutControl pressedCtrl,
    int sheetHoverIdx,
    FlyoutHitMap& outHitMap) {
    const WidgetDimensions dim = FlyoutDimensions();
    float scale = static_cast<float>(dpi) / 96.0f;
    int wPx = std::max(1, static_cast<int>(std::lround(dim.widthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(dim.heightDip * scale)));

    if (UsesDCompWindow(hwnd)) {
        SyncPopupWindowBounds(hwnd, ptScreen, wPx, hPx);
        return RenderToDCompSurface(hwnd, wPx, hPx, dpi, [&](ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brMain, ID2D1SolidColorBrush* brAux) {
            DrawFlyoutContent(rt, brMain, brAux, dpi, cfg, snap, history, gold, autoStartEnabled, displayOpts, anim, flyAnim, hoverCtrl, pressedCtrl, sheetHoverIdx, outHitMap);
        });
    }

    if (!EnsureWicTarget(m_flyoutTarget, wPx, hPx, dpi)) return false;

    ID2D1RenderTarget* rt = m_flyoutTarget.rt;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawFlyoutContent(rt, m_flyoutTarget.brMain, m_flyoutTarget.brAux, dpi, cfg, snap, history, gold, autoStartEnabled, displayOpts, anim, flyAnim, hoverCtrl, pressedCtrl, sheetHoverIdx, outHitMap);
    if (FAILED(rt->EndDraw())) return false;
    return PresentWicToWindow(hwnd, ptScreen, m_flyoutTarget.bitmap, wPx, hPx);
}
