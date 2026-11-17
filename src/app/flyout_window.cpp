#include "app/flyout_window.hpp"
#include "app/widget_window.hpp"
#include "app/menu_window.hpp"
#include <algorithm>
#include <cmath>

void HideFlyout(AppState* app) {
    if (!app->flyoutVisible) return;
    app->flyoutVisible = false;
    app->flyoutHoverBtn = -1;
    app->lastFlyoutClosedTick = GetTickCount64();
    ShowWindow(app->hwndFlyout, SW_HIDE);
}

void RenderAndPositionFlyout(AppState* app) {
    if (!app->flyoutVisible || !app->hwndFlyout) return;
    WidgetDimensions dim = RenderEngine::FlyoutDimensions();

    UINT dpi = app->lastPlacement.valid ? app->lastPlacement.dpi : GetDpiForSystem();
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;
    int fwPx = static_cast<int>(std::lround(dim.widthDip * scale));
    int fhPx = static_cast<int>(std::lround(dim.heightDip * scale));

    RECT rcAnchor = app->lastPlacement.rcTaskbar;
    RECT rcMon = app->lastPlacement.rcMonitor;
    if (!app->lastPlacement.valid) {
        POINT pt{};
        GetCursorPos(&pt);
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hMon, &mi);
        rcMon = mi.rcMonitor;
        rcAnchor = RECT{ pt.x - fwPx / 2, mi.rcWork.bottom, pt.x + fwPx / 2, mi.rcMonitor.bottom };
    }

    int centerX = app->lastPlacement.valid && app->lastPlacement.shouldShow
        ? (app->lastPlacement.ptWidget.x + app->lastPlacement.szWidget.cx / 2)
        : (rcMon.right - fwPx / 2 - 16);
    int gap = static_cast<int>(std::lround(10.0f * scale));
    int fx = std::clamp(centerX - fwPx / 2, static_cast<int>(rcMon.left) + gap, static_cast<int>(rcMon.right) - fwPx - gap);
    int fy = app->lastPlacement.taskbarAtTop
        ? (rcAnchor.bottom + gap)
        : (rcAnchor.top - fhPx - gap);
    fy = std::clamp(fy, static_cast<int>(rcMon.top) + gap, static_cast<int>(rcMon.bottom) - fhPx - gap);

    app->renderer.RenderFlyoutToWindow(
        app->hwndFlyout,
        POINT{ fx, fy },
        dpi,
        app->cfg,
        app->snap,
        app->sensors.History(),
        app->flyoutHoverBtn,
        app->flyoutActionRects);
}

void ShowFlyout(AppState* app) {
    if (!app->hwndFlyout) return;
    app->flyoutVisible = true;
    app->flyoutHoverBtn = -1;
    RenderAndPositionFlyout(app);
    ShowWindow(app->hwndFlyout, SW_SHOW);
    SetForegroundWindow(app->hwndFlyout);
}

void ToggleFlyout(AppState* app) {
    if (app->flyoutVisible) {
        HideFlyout(app);
        return;
    }
    if (GetTickCount64() - app->lastFlyoutClosedTick < 240) {
        return;
    }
    ShowFlyout(app);
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
        case WM_ACTIVATE: {
            HWND hNewActive = reinterpret_cast<HWND>(lParam);
            bool goingToMenu = (hNewActive != nullptr) && (hNewActive == app->hwndMenu || hNewActive == app->hwndSubmenu);
            if (LOWORD(wParam) == WA_INACTIVE && !app->suppressFlyoutDeactivate && !goingToMenu) {
                HideFlyout(app);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                HideFlyout(app);
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
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
                RenderAndPositionFlyout(app);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            app->flyoutTracking = false;
            if (app->flyoutHoverBtn != -1) {
                app->flyoutHoverBtn = -1;
                RenderAndPositionFlyout(app);
            }
            return 0;

        case WM_LBUTTONUP: {
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            int hit = -1;
            for (int i = 0; i < 4; ++i) {
                if (PtInRect(&app->flyoutActionRects[i], pt)) {
                    hit = i;
                    break;
                }
            }
            if (hit == 0) {
                if (app->menuVisible) CloseMenu(app, false);
                app->cfg.style = (app->cfg.style == VisualStyle::Capsule) ? VisualStyle::Minimal : VisualStyle::Capsule;
                SaveConfig(app->cfg);
                SyncWidgetPlacement(app, true);
                RenderAndPositionFlyout(app);
            } else if (hit == 1) {
                if (app->menuVisible) CloseMenu(app, false);
                if (app->cfg.theme == ThemeMode::Auto) app->cfg.theme = ThemeMode::Light;
                else if (app->cfg.theme == ThemeMode::Light) app->cfg.theme = ThemeMode::Dark;
                else app->cfg.theme = ThemeMode::Auto;
                SaveConfig(app->cfg);
                SyncWidgetPlacement(app, true);
                RenderAndPositionFlyout(app);
            } else if (hit == 2) {
                if (app->menuVisible) CloseMenu(app, false);
                app->cfg.showMiniBars = !app->cfg.showMiniBars;
                SaveConfig(app->cfg);
                SyncWidgetPlacement(app, true);
                RenderAndPositionFlyout(app);
            } else if (hit == 3) {
                POINT scr = pt;
                ClientToScreen(hwnd, &scr);
                ShowContextMenu(app, scr, true);
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
