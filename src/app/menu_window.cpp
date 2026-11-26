#include "app/menu_window.hpp"
#include "app/flyout_window.hpp"
#include "app/widget_window.hpp"
#include "resource.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

void SetClickThrough(HWND hwnd, bool enabled) {
    if (!hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR next = enabled ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    if (next != ex) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, next);
    }
}

int HitTestMenuItem(const std::vector<RECT>& rects, POINT pt) {
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!IsRectEmpty(&rects[i]) && PtInRect(&rects[i], pt)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int StepMenuFocus(const FluentMenuSpec& spec, int current, int dir) {
    if (spec.items.empty()) return -1;
    int n = static_cast<int>(spec.items.size());
    int idx = (current < 0) ? (dir > 0 ? -1 : 0) : current;
    for (int tries = 0; tries < n; ++tries) {
        idx = (idx + dir + n) % n;
        if (!spec.items[static_cast<size_t>(idx)].isSeparator) {
            return idx;
        }
    }
    return -1;
}

} // namespace

FluentMenuSpec BuildMainMenuSpec(const AppConfig& cfg, bool flyoutVisible) {
    FluentMenuSpec spec;
    spec.cardWidthDip = 224.0f;
    const wchar_t* barText = L"显示底部状态微条";

    spec.items = {
        { IDM_OPEN_FLYOUT, false, false, false, MenuCheckStyle::None, false,
          flyoutVisible ? L"收起性能概览面板" : L"打开性能概览面板" },
        { 0, true, false, false, MenuCheckStyle::None, false, L"" },
        { kSubStyle, false, false, true, MenuCheckStyle::None, false, L"外观风格" },
        { kSubTheme, false, false, true, MenuCheckStyle::None, false, L"配色主题" },
        { IDM_TOGGLE_BARS, false, false, false, MenuCheckStyle::Check, cfg.showMiniBars, barText },
        { kSubRow2, false, false, true, MenuCheckStyle::None, false, L"CPU 副行指标" },
        { IDM_TOGGLE_GPUTEMP, false, false, false, MenuCheckStyle::Check, cfg.showGpuTemp, L"显示 GPU 温度" },
        { IDM_TOGGLE_GOLD, false, false, false, MenuCheckStyle::Check, cfg.showGoldPrice, L"显示国际金价（元/克）" },
        { 0, true, false, false, MenuCheckStyle::None, false, L"" },
        { kSubDisplay, false, false, true, MenuCheckStyle::None, false, L"显示位置" },
        { kSubRate, false, false, true, MenuCheckStyle::None, false, L"刷新频率" },
        { IDM_RESET_OFFSET, false, false, false, MenuCheckStyle::None, false, L"重置水平位置偏移" },
        { 0, true, false, false, MenuCheckStyle::None, false, L"" },
        { IDM_TOGGLE_AUTOSTART, false, false, false, MenuCheckStyle::Check, IsAutoStartEnabled(), L"开机自动启动" },
        { 0, true, false, false, MenuCheckStyle::None, false, L"" },
        { IDM_EXIT, false, true, false, MenuCheckStyle::None, false, L"退出" },
    };
    return spec;
}

FluentMenuSpec BuildSubmenuSpec(AppState* app, UINT subId) {
    FluentMenuSpec spec;
    const AppConfig& cfg = app->cfg;
    if (subId == kSubStyle) {
        spec.cardWidthDip = 186.0f;
        spec.items = {
            { IDM_STYLE_CAPSULE, false, false, false, MenuCheckStyle::Radio, cfg.style == VisualStyle::Capsule, L"Win11 微胶囊风" },
            { IDM_STYLE_MINIMAL, false, false, false, MenuCheckStyle::Radio, cfg.style == VisualStyle::Minimal, L"极致无框原生风" },
        };
    } else if (subId == kSubTheme) {
        spec.cardWidthDip = 164.0f;
        spec.items = {
            { IDM_THEME_AUTO, false, false, false, MenuCheckStyle::Radio, cfg.theme == ThemeMode::Auto, L"跟随系统任务栏" },
            { IDM_THEME_LIGHT, false, false, false, MenuCheckStyle::Radio, cfg.theme == ThemeMode::Light, L"浅色模式" },
            { IDM_THEME_DARK, false, false, false, MenuCheckStyle::Radio, cfg.theme == ThemeMode::Dark, L"深色模式" },
        };
    } else if (subId == kSubRow2) {
        spec.cardWidthDip = 196.0f;
        spec.items = {
            { IDM_ROW2_CPU_FREQ, false, false, false, MenuCheckStyle::Radio, cfg.row2Col1 == Row2Col1Mode::CpuFreq, L"显示 CPU 实时主频" },
            { IDM_ROW2_CPU_TEMP, false, false, false, MenuCheckStyle::Radio, cfg.row2Col1 == Row2Col1Mode::CpuTemp, L"显示 CPU 温度" },
            { IDM_ROW2_MEM_PCT, false, false, false, MenuCheckStyle::Radio, cfg.row2Col1 == Row2Col1Mode::MemPct, L"显示内存占用率" },
            { IDM_ROW2_HIDE, false, false, false, MenuCheckStyle::Radio, cfg.row2Col1 == Row2Col1Mode::Hide, L"隐藏（仅显示 CPU 占用）" },
        };
    } else if (subId == kSubRate) {
        spec.cardWidthDip = 180.0f;
        spec.items = {
            { IDM_RATE_500, false, false, false, MenuCheckStyle::Radio, cfg.refreshMs == 500, L"0.5 秒（高灵敏）" },
            { IDM_RATE_1000, false, false, false, MenuCheckStyle::Radio, cfg.refreshMs == 1000, L"1.0 秒（推荐低占用）" },
            { IDM_RATE_2000, false, false, false, MenuCheckStyle::Radio, cfg.refreshMs == 2000, L"2.0 秒（极省电）" },
        };
    } else if (subId == kSubDisplay) {
        app->host.RefreshHandles(false);
        app->menuDisplayDevices.clear();

        auto formatMonitorDetail = [](const TaskbarInfo& tb) -> std::wstring {
            wchar_t res[48]{};
            int w = tb.rcMonitor.right - tb.rcMonitor.left;
            int h = tb.rcMonitor.bottom - tb.rcMonitor.top;
            if (w > 0 && h > 0) {
                _snwprintf_s(res, _countof(res), _TRUNCATE, L"(%d×%d)", w, h);
            }
            std::wstring out;
            if (!tb.friendlyName.empty()) {
                out += L" · ";
                out += tb.friendlyName;
            }
            if (res[0] != L'\0') {
                out += L" ";
                out += res;
            }
            return out;
        };

        std::wstring primaryLabel = L"仅主显示器";
        std::vector<const TaskbarInfo*> secondaryTaskbars;
        for (const auto& tb : app->host.Taskbars()) {
            if (!tb.hwndTaskbar || !IsWindow(tb.hwndTaskbar)) continue;
            if (tb.isPrimary) {
                primaryLabel += formatMonitorDetail(tb);
            } else if (IsWindowVisible(tb.hwndTaskbar)) {
                secondaryTaskbars.push_back(&tb);
            }
        }

        const std::wstring wantKey = SanitizeMonitorKey(cfg.displayDevice);
        bool specificMatched = false;

        spec.items.push_back({ IDM_DISPLAY_ALL, false, false, false, MenuCheckStyle::Radio,
            cfg.displayMode == DisplayTargetMode::All, L"所有显示器" });
        spec.items.push_back({ IDM_DISPLAY_PRIMARY, false, false, false, MenuCheckStyle::Radio,
            cfg.displayMode == DisplayTargetMode::Primary, primaryLabel });

        if (!secondaryTaskbars.empty() || cfg.displayMode == DisplayTargetMode::Specific) {
            spec.items.push_back({ 0, true, false, false, MenuCheckStyle::None, false, L"" });
        }

        int displayNum = 2;
        const size_t maxSpecific = static_cast<size_t>(IDM_DISPLAY_SPEC_MAX - IDM_DISPLAY_SPEC_BASE + 1);
        for (const TaskbarInfo* tb : secondaryTaskbars) {
            if (app->menuDisplayDevices.size() >= maxSpecific) break;
            UINT cmdId = IDM_DISPLAY_SPEC_BASE + static_cast<UINT>(app->menuDisplayDevices.size());
            app->menuDisplayDevices.push_back(tb->deviceName);
            bool checked = (cfg.displayMode == DisplayTargetMode::Specific &&
                SanitizeMonitorKey(tb->deviceName) == wantKey);
            if (checked) specificMatched = true;

            wchar_t prefix[32]{};
            _snwprintf_s(prefix, _countof(prefix), _TRUNCATE, L"显示器 %d", displayNum++);
            std::wstring label = std::wstring(prefix) + formatMonitorDetail(*tb);
            spec.items.push_back({ cmdId, false, false, false, MenuCheckStyle::Radio, checked, std::move(label) });
        }

        if (cfg.displayMode == DisplayTargetMode::Specific && !specificMatched) {
            spec.items.push_back({ 0, false, false, false, MenuCheckStyle::Radio, true,
                L"已选副屏（未连接，暂回落主屏）" });
        }

        float maxTextW = 0.0f;
        for (const auto& it : spec.items) {
            if (!it.isSeparator) {
                maxTextW = std::max(maxTextW, app->renderer.MeasureMenuLabelWidth(it.label.c_str()));
            }
        }
        spec.cardWidthDip = std::clamp(maxTextW + 46.0f, 196.0f, 340.0f);
    }
    return spec;
}

void RenderMainMenu(AppState* app) {
    if (!app->menuVisible || !app->hwndMenu) return;
    app->renderer.RenderMenuToWindow(
        app->hwndMenu, app->menuWinPos, app->menuDpi, app->cfg, app->menuSpec,
        app->menuHoverIdx, app->openSubIdx, app->menuAnim, false, app->menuItemRects);
}

void RenderSubmenu(AppState* app) {
    if (!app->submenuVisible || !app->hwndSubmenu) return;
    app->renderer.RenderMenuToWindow(
        app->hwndSubmenu, app->submenuWinPos, app->menuDpi, app->cfg, app->submenuSpec,
        app->submenuHoverIdx, -1, app->submenuAnim, true, app->submenuItemRects);
}

void HideSubmenu(AppState* app, bool animate) {
    if (app->hwndMenu) {
        KillTimer(app->hwndMenu, kTimerSubOpenId);
        KillTimer(app->hwndMenu, kTimerSubCloseId);
    }
    app->pendingSubIdx = -1;
    bool wasOpen = app->submenuVisible || app->openSubIdx != -1;
    bool wasVis = app->submenuVisible;
    app->submenuVisible = false;
    app->submenuHoverIdx = -1;
    app->openSubIdx = -1;
    app->submenuTracking = false;

    if (app->hwndSubmenu) {
        if (animate && wasVis && app->renderer.HasDComp() && IsWindowVisible(app->hwndSubmenu)) {
            SetClickThrough(app->hwndSubmenu, true);
            PopupMotionSpec m{};
            m.fromOpacity = 1.0f;
            m.toOpacity = 0.0f;
            m.fromScale = 1.0f;
            m.toScale = 0.99f;
            m.centerXRatio = 0.5f;
            m.centerYRatio = 0.5f;
            m.durationSec = 0.05f;
            m.easeOut = false;
            app->renderer.AnimateWindowVisual(app->hwndSubmenu, app->menuDpi, m);
            SetTimer(app->hwndSubmenu, kTimerSubmenuHideId, 56, nullptr);
        } else {
            KillTimer(app->hwndSubmenu, kTimerSubmenuHideId);
            SetClickThrough(app->hwndSubmenu, false);
            ShowWindow(app->hwndSubmenu, SW_HIDE);
        }
    }
    if (wasOpen && app->menuVisible) RenderMainMenu(app);
}

void CloseMenu(AppState* app, bool restoreFlyoutFocus, bool animate) {
    if (!app->menuVisible) {
        if (!animate) {
            if (app->hwndSubmenu) {
                KillTimer(app->hwndSubmenu, kTimerSubmenuHideId);
                SetClickThrough(app->hwndSubmenu, false);
                ShowWindow(app->hwndSubmenu, SW_HIDE);
            }
            if (app->hwndMenu) {
                KillTimer(app->hwndMenu, kTimerMenuHideId);
                SetClickThrough(app->hwndMenu, false);
                ShowWindow(app->hwndMenu, SW_HIDE);
            }
        }
        return;
    }
    if (app->hwndMenu) {
        KillTimer(app->hwndMenu, kTimerSubOpenId);
        KillTimer(app->hwndMenu, kTimerSubCloseId);
    }

    bool subWasVis = app->submenuVisible;
    app->submenuVisible = false;
    app->submenuHoverIdx = -1;
    app->openSubIdx = -1;
    app->pendingSubIdx = -1;
    app->submenuTracking = false;

    if (app->hwndSubmenu) {
        if (animate && subWasVis && app->renderer.HasDComp() && IsWindowVisible(app->hwndSubmenu)) {
            SetClickThrough(app->hwndSubmenu, true);
            PopupMotionSpec ms{};
            ms.fromOpacity = 1.0f;
            ms.toOpacity = 0.0f;
            ms.fromScale = 1.0f;
            ms.toScale = 0.99f;
            ms.durationSec = 0.055f;
            ms.easeOut = false;
            app->renderer.AnimateWindowVisual(app->hwndSubmenu, app->menuDpi, ms);
            SetTimer(app->hwndSubmenu, kTimerSubmenuHideId, 60, nullptr);
        } else {
            KillTimer(app->hwndSubmenu, kTimerSubmenuHideId);
            SetClickThrough(app->hwndSubmenu, false);
            ShowWindow(app->hwndSubmenu, SW_HIDE);
        }
    }

    app->menuVisible = false;
    app->menuHoverIdx = -1;
    app->menuTracking = false;

    if (app->hwndMenu) {
        if (animate && app->renderer.HasDComp() && IsWindowVisible(app->hwndMenu)) {
            SetClickThrough(app->hwndMenu, true);
            PopupMotionSpec mm{};
            mm.fromOpacity = 1.0f;
            mm.toOpacity = 0.0f;
            mm.fromScale = 1.0f;
            mm.toScale = 0.99f;
            mm.durationSec = 0.055f;
            mm.easeOut = false;
            app->renderer.AnimateWindowVisual(app->hwndMenu, app->menuDpi, mm);
            SetTimer(app->hwndMenu, kTimerMenuHideId, 60, nullptr);
        } else {
            KillTimer(app->hwndMenu, kTimerMenuHideId);
            SetClickThrough(app->hwndMenu, false);
            ShowWindow(app->hwndMenu, SW_HIDE);
        }
    }

    if (restoreFlyoutFocus && app->flyoutVisible && app->hwndFlyout) {
        SetForegroundWindow(app->hwndFlyout);
    }
}

void OpenSubmenuForRow(AppState* app, int rowIdx, bool selectFirst) {
    if (!app->menuVisible || !app->hwndMenu || !app->hwndSubmenu) return;
    if (rowIdx < 0 || rowIdx >= static_cast<int>(app->menuSpec.items.size())) return;
    const auto& item = app->menuSpec.items[static_cast<size_t>(rowIdx)];
    if (!item.hasSubmenu) return;

    KillTimer(app->hwndMenu, kTimerSubOpenId);
    KillTimer(app->hwndMenu, kTimerSubCloseId);
    KillTimer(app->hwndSubmenu, kTimerSubmenuHideId);
    SetClickThrough(app->hwndSubmenu, false);
    app->pendingSubIdx = -1;

    app->submenuSpec = BuildSubmenuSpec(app, item.cmdId);
    app->openSubIdx = rowIdx;
    app->submenuHoverIdx = selectFirst ? 0 : -1;
    app->submenuAnim = {};
    if (selectFirst && !app->submenuSpec.items.empty()) {
        app->submenuAnim.pillY = RenderEngine::MenuRowTopDip(app->submenuSpec, 0);
        app->submenuAnim.pillAlpha = 1.0f;
        app->submenuAnim.hasPillPos = true;
    }

    float scale = static_cast<float>(app->menuDpi) / 96.0f;
    WidgetDimensions subDim = RenderEngine::MeasureMenu(app->submenuSpec);
    int subWinW = static_cast<int>(std::lround(subDim.widthDip * scale));
    int subWinH = static_cast<int>(std::lround(subDim.heightDip * scale));
    int padXPx = static_cast<int>(std::lround(RenderEngine::MenuPadX() * scale));
    int padTopPx = static_cast<int>(std::lround(RenderEngine::MenuPadTop() * scale));
    int padBotPx = static_cast<int>(std::lround(RenderEngine::MenuPadBottom() * scale));
    int subCardW = subWinW - padXPx * 2;
    int subCardH = subWinH - padTopPx - padBotPx;

    WidgetDimensions mainDim = RenderEngine::MeasureMenu(app->menuSpec);
    int mainWinW = static_cast<int>(std::lround(mainDim.widthDip * scale));
    int mainCardLeft = app->menuWinPos.x + padXPx;
    int mainCardRight = app->menuWinPos.x + mainWinW - padXPx;
    int gapPx = static_cast<int>(std::lround(4.0f * scale));
    int marginPx = static_cast<int>(std::lround(6.0f * scale));

    int rowTopScreen = app->menuWinPos.y +
        ((rowIdx < static_cast<int>(app->menuItemRects.size())) ? app->menuItemRects[rowIdx].top : padTopPx);
    int cardY = rowTopScreen - static_cast<int>(std::lround(6.0f * scale));
    int cardX = mainCardRight + gapPx;

    HMONITOR hMon = MonitorFromPoint(POINT{ mainCardLeft, rowTopScreen }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMon, &mi);

    bool openedLeft = false;
    if (cardX + subCardW > mi.rcMonitor.right - marginPx) {
        int altX = mainCardLeft - gapPx - subCardW;
        if (altX >= mi.rcMonitor.left + marginPx) {
            cardX = altX;
            openedLeft = true;
        }
    }
    cardX = std::clamp(cardX, static_cast<int>(mi.rcMonitor.left) + marginPx, static_cast<int>(mi.rcMonitor.right) - subCardW - marginPx);
    cardY = std::clamp(cardY, static_cast<int>(mi.rcMonitor.top) + marginPx, static_cast<int>(mi.rcMonitor.bottom) - subCardH - marginPx);

    app->submenuWinPos = POINT{ cardX - padXPx, cardY - padTopPx };
    app->submenuVisible = true;

    RenderMainMenu(app);
    RenderSubmenu(app);

    if (app->renderer.HasDComp()) {
        PopupMotionSpec m{};
        m.fromOpacity = 0.50f;
        m.toOpacity = 1.0f;
        m.fromOffsetXDip = openedLeft ? 5.0f : -5.0f;
        m.toOffsetXDip = 0.0f;
        m.fromScale = 0.988f;
        m.toScale = 1.0f;
        m.centerXRatio = openedLeft ? 1.0f : 0.0f;
        m.centerYRatio = 0.5f;
        m.durationSec = 0.075f;
        m.easeOut = true;
        app->renderer.AnimateWindowVisual(app->hwndSubmenu, app->menuDpi, m);
    }

    SetWindowPos(app->hwndSubmenu, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void ShowContextMenu(AppState* app, POINT ptScreen, bool openedFromFlyout, HWND anchorWidget) {
    if (!app->hwndMenu) return;

    if (app->menuVisible && openedFromFlyout) {
        CloseMenu(app, true, true);
        return;
    }
    CloseMenu(app, false, false);
    KillTimer(app->hwndMenu, kTimerMenuHideId);
    SetClickThrough(app->hwndMenu, false);

    HMONITOR hMon = MonitorFromPoint(ptScreen, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMon, &mi);

    UINT dpi = 0;
    if (const WidgetInstance* w = FindWidget(app, anchorWidget)) {
        if (w->lastPlacement.valid) dpi = w->lastPlacement.dpi;
    }
    if (dpi == 0) {
        for (const auto& w : app->widgets) {
            if (w.lastPlacement.valid && EqualRect(&w.lastPlacement.rcMonitor, &mi.rcMonitor)) {
                dpi = w.lastPlacement.dpi;
                break;
            }
        }
    }
    if (dpi == 0) {
        for (const auto& tb : app->host.Taskbars()) {
            if (tb.hMonitor == hMon && tb.dpi != 0) {
                dpi = tb.dpi;
                break;
            }
        }
    }
    if (dpi == 0) {
        const WidgetInstance* pref = FindPreferredWidget(app);
        dpi = (pref && pref->lastPlacement.valid) ? pref->lastPlacement.dpi : GetDpiForSystem();
    }
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;

    app->menuDpi = dpi;
    app->menuSpec = BuildMainMenuSpec(app->cfg, app->flyoutVisible);
    app->menuHoverIdx = -1;
    app->openSubIdx = -1;
    app->pendingSubIdx = -1;
    app->submenuVisible = false;
    app->submenuHoverIdx = -1;
    app->menuAnim = {};
    app->submenuAnim = {};

    WidgetDimensions dim = RenderEngine::MeasureMenu(app->menuSpec);
    int winW = static_cast<int>(std::lround(dim.widthDip * scale));
    int winH = static_cast<int>(std::lround(dim.heightDip * scale));
    int padXPx = static_cast<int>(std::lround(RenderEngine::MenuPadX() * scale));
    int padTopPx = static_cast<int>(std::lround(RenderEngine::MenuPadTop() * scale));
    int padBotPx = static_cast<int>(std::lround(RenderEngine::MenuPadBottom() * scale));
    int cardW = winW - padXPx * 2;
    int cardH = winH - padTopPx - padBotPx;
    int marginPx = static_cast<int>(std::lround(6.0f * scale));

    int cardX = ptScreen.x;
    int cardY = ptScreen.y;
    bool openedUpward = false;

    if (openedFromFlyout && app->flyoutVisible && app->hwndFlyout) {
        RECT rcFly{};
        GetWindowRect(app->hwndFlyout, &rcFly);
        int flyPadX = static_cast<int>(std::lround(6.0f * scale));
        int flyPadBot = static_cast<int>(std::lround(7.0f * scale));
        int flyCardLeft = rcFly.left + flyPadX;
        int flyCardRight = rcFly.right - flyPadX;
        int flyCardBot = rcFly.bottom - flyPadBot;
        int gapPx = static_cast<int>(std::lround(6.0f * scale));

        if (flyCardRight + gapPx + cardW <= mi.rcMonitor.right - marginPx) {
            cardX = flyCardRight + gapPx;
            cardY = flyCardBot - cardH;
            openedUpward = true;
        } else if (flyCardLeft - gapPx - cardW >= mi.rcMonitor.left + marginPx) {
            cardX = flyCardLeft - gapPx - cardW;
            cardY = flyCardBot - cardH;
            openedUpward = true;
        } else {
            if (cardY + cardH > mi.rcWork.bottom - marginPx) {
                cardY = ptScreen.y - cardH;
                openedUpward = true;
            }
            if (cardX + cardW > mi.rcMonitor.right - marginPx) cardX = ptScreen.x - cardW;
        }
    } else {
        if (cardY + cardH > mi.rcWork.bottom - marginPx) {
            cardY = ptScreen.y - cardH;
            openedUpward = true;
        }
        if (cardX + cardW > mi.rcMonitor.right - marginPx) {
            cardX = ptScreen.x - cardW;
        }
    }

    cardX = std::clamp(cardX, static_cast<int>(mi.rcMonitor.left) + marginPx, static_cast<int>(mi.rcMonitor.right) - cardW - marginPx);
    cardY = std::clamp(cardY, static_cast<int>(mi.rcMonitor.top) + marginPx, static_cast<int>(mi.rcMonitor.bottom) - cardH - marginPx);

    app->menuWinPos = POINT{ cardX - padXPx, cardY - padTopPx };
    app->menuVisible = true;

    RenderMainMenu(app);

    if (app->renderer.HasDComp()) {
        PopupMotionSpec m{};
        m.fromOpacity = 0.50f;
        m.toOpacity = 1.0f;
        m.fromOffsetYDip = openedUpward ? 5.0f : -5.0f;
        m.toOffsetYDip = 0.0f;
        m.fromScale = 0.985f;
        m.toScale = 1.0f;
        m.centerXRatio = 0.5f;
        m.centerYRatio = openedUpward ? 1.0f : 0.0f;
        m.durationSec = 0.085f;
        m.easeOut = true;
        app->renderer.AnimateWindowVisual(app->hwndMenu, app->menuDpi, m);
    }

    SetWindowPos(app->hwndMenu, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(app->hwndMenu);
    PostMessageW(app->hwndMenu, WM_NULL, 0, 0);
}

LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = GetApp(hwnd);
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                HWND hNewActive = reinterpret_cast<HWND>(lParam);
                bool stayFlyout = (app->flyoutVisible && hNewActive == app->hwndFlyout);
                CloseMenu(app, false, true);
                if (app->flyoutVisible && !stayFlyout) {
                    HideFlyout(app, true);
                }
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == kTimerMenuHideId) {
                KillTimer(hwnd, kTimerMenuHideId);
                if (!app->menuVisible) {
                    SetClickThrough(hwnd, false);
                    ShowWindow(hwnd, SW_HIDE);
                }
            } else if (wParam == kTimerSubOpenId) {
                KillTimer(hwnd, kTimerSubOpenId);
                if (app->pendingSubIdx >= 0 && app->pendingSubIdx == app->menuHoverIdx) {
                    OpenSubmenuForRow(app, app->pendingSubIdx, false);
                }
            } else if (wParam == kTimerSubCloseId) {
                KillTimer(hwnd, kTimerSubCloseId);
                if (app->menuHoverIdx != app->openSubIdx && app->submenuHoverIdx < 0) {
                    HideSubmenu(app, true);
                }
            }
            return 0;

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                CloseMenu(app, true, true);
                return 0;
            }
            if (wParam == VK_LEFT && app->submenuVisible) {
                HideSubmenu(app, true);
                return 0;
            }
            if (wParam == VK_DOWN || wParam == VK_UP) {
                int dir = (wParam == VK_DOWN) ? 1 : -1;
                if (app->submenuVisible && app->submenuHoverIdx >= 0) {
                    app->submenuHoverIdx = StepMenuFocus(app->submenuSpec, app->submenuHoverIdx, dir);
                    KickInteractiveAnimation(app, app->hwndSubmenu);
                } else {
                    KillTimer(hwnd, kTimerSubOpenId);
                    app->menuHoverIdx = StepMenuFocus(app->menuSpec, app->menuHoverIdx, dir);
                    KickInteractiveAnimation(app, hwnd);
                }
                return 0;
            }
            if (wParam == VK_RIGHT && app->menuHoverIdx >= 0 &&
                app->menuHoverIdx < static_cast<int>(app->menuSpec.items.size()) &&
                app->menuSpec.items[static_cast<size_t>(app->menuHoverIdx)].hasSubmenu) {
                OpenSubmenuForRow(app, app->menuHoverIdx, true);
                return 0;
            }
            if (wParam == VK_RETURN) {
                if (app->submenuVisible && app->submenuHoverIdx >= 0 &&
                    app->submenuHoverIdx < static_cast<int>(app->submenuSpec.items.size())) {
                    UINT cmd = app->submenuSpec.items[static_cast<size_t>(app->submenuHoverIdx)].cmdId;
                    CloseMenu(app, cmd != IDM_OPEN_FLYOUT && cmd != IDM_EXIT, cmd != IDM_EXIT);
                    ExecuteMenuCommand(app, cmd);
                    return 0;
                }
                if (app->menuHoverIdx >= 0 && app->menuHoverIdx < static_cast<int>(app->menuSpec.items.size())) {
                    const auto& it = app->menuSpec.items[static_cast<size_t>(app->menuHoverIdx)];
                    if (it.hasSubmenu) {
                        OpenSubmenuForRow(app, app->menuHoverIdx, true);
                    } else if (!it.isSeparator) {
                        UINT cmd = it.cmdId;
                        CloseMenu(app, cmd != IDM_OPEN_FLYOUT && cmd != IDM_EXIT, cmd != IDM_EXIT);
                        ExecuteMenuCommand(app, cmd);
                    }
                }
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            if (!app->menuVisible) return 0;
            if (!app->menuTracking) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                app->menuTracking = true;
            }
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int hit = HitTestMenuItem(app->menuItemRects, pt);
            if (hit != app->menuHoverIdx) {
                app->menuHoverIdx = hit;
                KillTimer(hwnd, kTimerSubOpenId);
                if (hit >= 0 && hit < static_cast<int>(app->menuSpec.items.size()) &&
                    app->menuSpec.items[static_cast<size_t>(hit)].hasSubmenu) {
                    KillTimer(hwnd, kTimerSubCloseId);
                    if (hit != app->openSubIdx) {
                        app->pendingSubIdx = hit;
                        SetTimer(hwnd, kTimerSubOpenId, 35, nullptr);
                    }
                } else {
                    app->pendingSubIdx = -1;
                    if (app->submenuVisible) {
                        SetTimer(hwnd, kTimerSubCloseId, 180, nullptr);
                    }
                }
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            app->menuTracking = false;
            KillTimer(hwnd, kTimerSubOpenId);
            app->pendingSubIdx = -1;
            if (app->menuHoverIdx != -1) {
                app->menuHoverIdx = -1;
                KickInteractiveAnimation(app, hwnd);
            }
            if (app->submenuVisible) {
                SetTimer(hwnd, kTimerSubCloseId, 180, nullptr);
            }
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            if (!app->menuVisible) return 0;
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int hit = HitTestMenuItem(app->menuItemRects, pt);
            if (hit >= 0 && hit < static_cast<int>(app->menuSpec.items.size())) {
                const auto& it = app->menuSpec.items[static_cast<size_t>(hit)];
                if (it.hasSubmenu) {
                    OpenSubmenuForRow(app, hit, false);
                } else if (!it.isSeparator) {
                    UINT cmd = it.cmdId;
                    CloseMenu(app, cmd != IDM_OPEN_FLYOUT && cmd != IDM_EXIT, cmd != IDM_EXIT);
                    ExecuteMenuCommand(app, cmd);
                }
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SubmenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = GetApp(hwnd);
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            if (wParam == kTimerSubmenuHideId) {
                KillTimer(hwnd, kTimerSubmenuHideId);
                if (!app->submenuVisible) {
                    SetClickThrough(hwnd, false);
                    ShowWindow(hwnd, SW_HIDE);
                }
                return 0;
            }
            break;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_MOUSEMOVE: {
            if (!app->submenuVisible) return 0;
            if (app->hwndMenu) KillTimer(app->hwndMenu, kTimerSubCloseId);
            if (!app->submenuTracking) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                app->submenuTracking = true;
            }
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int hit = HitTestMenuItem(app->submenuItemRects, pt);
            if (hit != app->submenuHoverIdx) {
                app->submenuHoverIdx = hit;
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            app->submenuTracking = false;
            if (app->submenuHoverIdx != -1) {
                app->submenuHoverIdx = -1;
                KickInteractiveAnimation(app, hwnd);
            }
            if (app->hwndMenu && app->submenuVisible) {
                SetTimer(app->hwndMenu, kTimerSubCloseId, 180, nullptr);
            }
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            if (!app->submenuVisible) return 0;
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int hit = HitTestMenuItem(app->submenuItemRects, pt);
            if (hit >= 0 && hit < static_cast<int>(app->submenuSpec.items.size())) {
                UINT cmd = app->submenuSpec.items[static_cast<size_t>(hit)].cmdId;
                CloseMenu(app, cmd != IDM_OPEN_FLYOUT && cmd != IDM_EXIT, cmd != IDM_EXIT);
                ExecuteMenuCommand(app, cmd);
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
