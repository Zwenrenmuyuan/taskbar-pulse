#include "app/flyout_window.hpp"
#include "app/widget_window.hpp"
#include "app/menu_window.hpp"
#include <algorithm>
#include <cmath>

namespace {

void SetClickThrough(HWND hwnd, bool enabled) {
    if (!hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR next = enabled ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
    if (next != ex) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, next);
    }
}

} // namespace

void HideFlyout(AppState* app, bool animate) {
    if (!app || !app->hwndFlyout) return;
    if (!app->flyoutVisible) {
        if (!animate) {
            KillTimer(app->hwndFlyout, kTimerFlyoutHideId);
            SetClickThrough(app->hwndFlyout, false);
            ShowWindow(app->hwndFlyout, SW_HIDE);
        }
        return;
    }
    app->flyoutVisible = false;
    app->flyoutHoverBtn = -1;
    app->flyoutPressedBtn = -1;
    app->flyoutTracking = false;
    app->menuOpenOnFlyoutMouseActivate = false;
    app->suppressNextMoreBtnClick = false;
    app->lastFlyoutClosedTick = GetTickCount64();

    if (animate && app->renderer.HasDComp() && IsWindowVisible(app->hwndFlyout)) {
        SetClickThrough(app->hwndFlyout, true);
        PopupMotionSpec m{};
        m.fromOpacity = 1.0f;
        m.toOpacity = 0.0f;
        m.fromOffsetYDip = 0.0f;
        m.toOffsetYDip = app->flyoutTaskbarAtTop ? -5.0f : 5.0f;
        m.fromScale = 1.0f;
        m.toScale = 0.99f;
        m.centerXRatio = 0.5f;
        m.centerYRatio = app->flyoutTaskbarAtTop ? 0.0f : 1.0f;
        m.durationSec = 0.065f;
        m.easeOut = false;
        app->renderer.AnimateWindowVisual(app->hwndFlyout, app->flyoutDpi, m);
        SetTimer(app->hwndFlyout, kTimerFlyoutHideId, 72, nullptr);
    } else {
        KillTimer(app->hwndFlyout, kTimerFlyoutHideId);
        SetClickThrough(app->hwndFlyout, false);
        ShowWindow(app->hwndFlyout, SW_HIDE);
    }
}

void RenderAndPositionFlyout(AppState* app) {
    if (!app->flyoutVisible || !app->hwndFlyout) return;
    WidgetDimensions dim = RenderEngine::FlyoutDimensions();

    const WidgetInstance* anchor = FindPreferredWidget(app, app->flyoutAnchorWidget);
    bool hasPlacement = (anchor != nullptr && anchor->lastPlacement.valid);

    UINT dpi = hasPlacement ? anchor->lastPlacement.dpi : GetDpiForSystem();
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;
    int fwPx = static_cast<int>(std::lround(dim.widthDip * scale));
    int fhPx = static_cast<int>(std::lround(dim.heightDip * scale));

    RECT rcAnchor{};
    RECT rcMon{};
    bool taskbarAtTop = false;
    int centerX = 0;

    if (hasPlacement) {
        const TaskbarPlacement& p = anchor->lastPlacement;
        rcAnchor = p.rcTaskbar;
        rcMon = p.rcMonitor;
        taskbarAtTop = p.taskbarAtTop;
        centerX = p.shouldShow
            ? (p.ptWidget.x + p.szWidget.cx / 2)
            : (rcMon.right - fwPx / 2 - 16);
    } else {
        POINT pt{};
        GetCursorPos(&pt);
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hMon, &mi);
        rcMon = mi.rcMonitor;
        rcAnchor = RECT{ pt.x - fwPx / 2, mi.rcWork.bottom, pt.x + fwPx / 2, mi.rcMonitor.bottom };
        centerX = rcMon.right - fwPx / 2 - 16;
    }

    app->flyoutDpi = dpi;
    app->flyoutTaskbarAtTop = taskbarAtTop;

    int gap = static_cast<int>(std::lround(10.0f * scale));
    int fx = std::clamp(centerX - fwPx / 2, static_cast<int>(rcMon.left) + gap, static_cast<int>(rcMon.right) - fwPx - gap);
    int fy = taskbarAtTop
        ? (rcAnchor.bottom + gap)
        : (rcAnchor.top - fhPx - gap);
    fy = std::clamp(fy, static_cast<int>(rcMon.top) + gap, static_cast<int>(rcMon.bottom) - fhPx - gap);

    app->renderer.RenderFlyoutToWindow(
        app->hwndFlyout,
        POINT{ fx, fy },
        dpi,
        app->cfg,
        app->snap,
        app->history,
        app->anim,
        app->flyoutAnim,
        app->flyoutActionRects);
}

void ShowFlyout(AppState* app, HWND anchorWidget) {
    if (!app->hwndFlyout) return;
    bool wasVisible = app->flyoutVisible;
    KillTimer(app->hwndFlyout, kTimerFlyoutHideId);
    SetClickThrough(app->hwndFlyout, false);

    app->flyoutAnchorWidget = anchorWidget;
    app->flyoutVisible = true;
    app->flyoutHoverBtn = -1;
    app->flyoutPressedBtn = -1;
    app->menuOpenOnFlyoutMouseActivate = false;
    app->suppressNextMoreBtnClick = false;
    app->flyoutAnim = {};

    RenderAndPositionFlyout(app);

    if (app->renderer.HasDComp()) {
        if (!wasVisible) {
            PopupMotionSpec m{};
            m.fromOpacity = 0.45f;
            m.toOpacity = 1.0f;
            m.fromOffsetYDip = app->flyoutTaskbarAtTop ? -7.0f : 7.0f;
            m.toOffsetYDip = 0.0f;
            m.fromScale = 0.985f;
            m.toScale = 1.0f;
            m.centerXRatio = 0.5f;
            m.centerYRatio = app->flyoutTaskbarAtTop ? 0.0f : 1.0f;
            m.durationSec = 0.11f;
            m.easeOut = true;
            app->renderer.AnimateWindowVisual(app->hwndFlyout, app->flyoutDpi, m);
        } else {
            app->renderer.ResetWindowVisual(app->hwndFlyout);
        }
    }

    SetWindowPos(app->hwndFlyout, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(app->hwndFlyout);
}

void ToggleFlyout(AppState* app, HWND anchorWidget) {
    if (app->flyoutVisible) {
        if (anchorWidget && anchorWidget != app->flyoutAnchorWidget) {
            ShowFlyout(app, anchorWidget);
            return;
        }
        HideFlyout(app, true);
        return;
    }
    if (GetTickCount64() - app->lastFlyoutClosedTick < 140) {
        return;
    }
    ShowFlyout(app, anchorWidget);
}

LRESULT CALLBACK FlyoutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
            if (wParam == kTimerFlyoutHideId) {
                KillTimer(hwnd, kTimerFlyoutHideId);
                if (!app->flyoutVisible) {
                    SetClickThrough(hwnd, false);
                    ShowWindow(hwnd, SW_HIDE);
                }
                return 0;
            }
            break;

        case WM_MOUSEACTIVATE: {
            UINT mouseMsg = HIWORD(lParam);
            app->menuOpenOnFlyoutMouseActivate =
                (mouseMsg == WM_LBUTTONDOWN || mouseMsg == WM_LBUTTONDBLCLK) && app->menuVisible;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_ACTIVATE: {
            HWND hNewActive = reinterpret_cast<HWND>(lParam);
            bool goingToMenu = (hNewActive != nullptr) && (hNewActive == app->hwndMenu || hNewActive == app->hwndSubmenu);
            if (LOWORD(wParam) == WA_INACTIVE && !app->suppressFlyoutDeactivate && !goingToMenu) {
                HideFlyout(app, true);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                HideFlyout(app, true);
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
            if (!app->flyoutVisible) return 0;
            if (!app->flyoutTracking) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                app->flyoutTracking = true;
            }
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int hit = -1;
            for (int i = 0; i < 4; ++i) {
                if (PtInRect(&app->flyoutActionRects[i], pt)) {
                    hit = i;
                    break;
                }
            }
            SetCursor(LoadCursorW(nullptr, hit >= 0 ? IDC_HAND : IDC_ARROW));
            if (hit != app->flyoutHoverBtn) {
                app->flyoutHoverBtn = hit;
                if (app->flyoutPressedBtn != -1 && app->flyoutPressedBtn != hit) {
                    app->flyoutPressedBtn = -1;
                }
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            app->flyoutTracking = false;
            app->menuOpenOnFlyoutMouseActivate = false;
            app->suppressNextMoreBtnClick = false;
            if (app->flyoutHoverBtn != -1 || app->flyoutPressedBtn != -1) {
                app->flyoutHoverBtn = -1;
                app->flyoutPressedBtn = -1;
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            if (!app->flyoutVisible) return 0;
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int hit = -1;
            for (int i = 0; i < 4; ++i) {
                if (PtInRect(&app->flyoutActionRects[i], pt)) {
                    hit = i;
                    break;
                }
            }
            bool menuWasOpen = app->menuVisible || app->menuOpenOnFlyoutMouseActivate;
            app->menuOpenOnFlyoutMouseActivate = false;
            if (hit == 3 && menuWasOpen) {
                app->suppressNextMoreBtnClick = true;
                if (app->menuVisible) CloseMenu(app, false, true);
            } else {
                app->suppressNextMoreBtnClick = false;
            }
            if (hit != app->flyoutPressedBtn) {
                app->flyoutPressedBtn = hit;
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!app->flyoutVisible) return 0;
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int hit = -1;
            for (int i = 0; i < 4; ++i) {
                if (PtInRect(&app->flyoutActionRects[i], pt)) {
                    hit = i;
                    break;
                }
            }
            bool suppressMore = app->suppressNextMoreBtnClick;
            app->menuOpenOnFlyoutMouseActivate = false;
            app->suppressNextMoreBtnClick = false;
            if (app->flyoutPressedBtn != -1) {
                app->flyoutPressedBtn = -1;
                KickInteractiveAnimation(app, hwnd);
            }
            if (hit == 0) {
                if (app->menuVisible) CloseMenu(app, false, true);
                app->cfg.style = (app->cfg.style == VisualStyle::Capsule) ? VisualStyle::Minimal : VisualStyle::Capsule;
                SyncAnimatedMetricTargetsImmediate(app);
                RenderAndPositionFlyout(app);
                SyncWidgetPlacement(app, true);
                SaveConfig(app->cfg);
            } else if (hit == 1) {
                if (app->menuVisible) CloseMenu(app, false, true);
                if (app->cfg.theme == ThemeMode::Auto) app->cfg.theme = ThemeMode::Light;
                else if (app->cfg.theme == ThemeMode::Light) app->cfg.theme = ThemeMode::Dark;
                else app->cfg.theme = ThemeMode::Auto;
                SyncAnimatedMetricTargetsImmediate(app);
                RenderAndPositionFlyout(app);
                SyncWidgetPlacement(app, true);
                SaveConfig(app->cfg);
            } else if (hit == 2) {
                if (app->menuVisible) CloseMenu(app, false, true);
                app->cfg.showMiniBars = !app->cfg.showMiniBars;
                RenderAndPositionFlyout(app);
                SyncWidgetPlacement(app, true);
                SaveConfig(app->cfg);
            } else if (hit == 3) {
                if (suppressMore) return 0;
                POINT scr = pt;
                ClientToScreen(hwnd, &scr);
                ShowContextMenu(app, scr, true);
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
