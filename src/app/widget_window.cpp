#include "app/widget_window.hpp"
#include "app/flyout_window.hpp"
#include "app/menu_window.hpp"
#include <cmath>

void EnsureWidgetWindow(AppState* app) {
    if (!app) return;
    app->host.RefreshHandles();
    HWND hTaskbar = app->host.TaskbarHwnd();
    if (!hTaskbar || !IsWindow(hTaskbar)) {
        return;
    }

    if (app->hwndWidget && (!IsWindow(app->hwndWidget) || GetParent(app->hwndWidget) != hTaskbar)) {
        app->renderer.ResetWidgetTarget();
        DestroyWindow(app->hwndWidget);
        app->hwndWidget = nullptr;
        app->widgetVisible = false;
        app->widgetHovered = false;
        app->widgetTracking = false;
        app->widgetPressed = false;
        app->draggingOffset = false;
        app->lastPlacement = {};
    }

    if (!app->hwndWidget) {
        app->hwndWidget = CreateWindowExW(
            WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE,
            kWidgetClass, L"TaskbarMonitorWidget",
            WS_CHILD | WS_CLIPSIBLINGS,
            0, 0, 10, 10, hTaskbar, nullptr, app->hInst, app);
        app->widgetVisible = false;
        app->widgetHovered = false;
        app->widgetTracking = false;
        app->widgetPressed = false;
        app->draggingOffset = false;
        app->lastPlacement = {};
    }
}

void RenderWidget(AppState* app) {
    if (!app->hwndWidget || !app->widgetVisible || !app->lastPlacement.shouldShow) return;
    app->renderer.RenderWidgetToWindow(
        app->hwndWidget,
        app->lastPlacement.ptWidget,
        app->lastPlacement.dpi,
        app->cfg,
        app->snap,
        app->sensors.History(),
        app->goldPrice.Snapshot(),
        app->widgetHovered,
        app->widgetPressed);
}

void SyncWidgetPlacement(AppState* app, bool forceRender) {
    EnsureWidgetWindow(app);
    if (!app->hwndWidget) return;

    WidgetDimensions dim = app->renderer.MeasureWidget(app->cfg);
    TaskbarPlacement p = app->host.ComputePlacement(
        dim.widthDip, dim.heightDip,
        app->cfg.offsetXDip, app->cfg.hideInFullscreen,
        app->hwndWidget, app->hwndFlyout);

    bool prevVis = app->widgetVisible;
    bool changedPosOrDpi = !app->lastPlacement.valid ||
        p.ptWidgetClient.x != app->lastPlacement.ptWidgetClient.x ||
        p.ptWidgetClient.y != app->lastPlacement.ptWidgetClient.y ||
        p.szWidget.cx != app->lastPlacement.szWidget.cx ||
        p.szWidget.cy != app->lastPlacement.szWidget.cy ||
        p.dpi != app->lastPlacement.dpi;

    app->lastPlacement = p;

    if (!p.valid || !p.shouldShow) {
        if (app->widgetVisible) {
            app->widgetVisible = false;
            ShowWindow(app->hwndWidget, SW_HIDE);
        }
        return;
    }

    bool needResizeOrShow = !prevVis || changedPosOrDpi;
    if (needResizeOrShow) {
        SetWindowPos(app->hwndWidget, HWND_TOP,
            p.ptWidgetClient.x, p.ptWidgetClient.y,
            p.szWidget.cx, p.szWidget.cy,
            SWP_NOACTIVATE | SWP_NOREDRAW);
    }

    if (needResizeOrShow || forceRender) {
        app->widgetVisible = true;
        RenderWidget(app);
    }

    if (!prevVis) {
        ShowWindow(app->hwndWidget, SW_SHOWNOACTIVATE);
    }

    app->host.EnsureAboveTaskbar(app->hwndWidget, app->hwndFlyout);
}

LRESULT CALLBACK WidgetWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_MOUSEMOVE: {
            if (app->draggingOffset) {
                POINT pt{};
                GetCursorPos(&pt);
                float scale = static_cast<float>(app->lastPlacement.dpi ? app->lastPlacement.dpi : 96) / 96.0f;
                int deltaDip = static_cast<int>(std::lround(static_cast<float>(pt.x - app->dragStartScreen.x) / scale));
                app->cfg.offsetXDip = app->dragStartOffsetDip + deltaDip;
                SyncWidgetPlacement(app, true);
                if (app->lastPlacement.shouldShow &&
                    app->cfg.offsetXDip != app->lastPlacement.appliedOffsetXDip) {
                    app->cfg.offsetXDip = app->lastPlacement.appliedOffsetXDip;
                    app->dragStartOffsetDip = app->cfg.offsetXDip;
                    app->dragStartScreen = pt;
                }
                return 0;
            }
            if (!app->widgetTracking) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                app->widgetTracking = true;
            }
            if (!app->widgetHovered) {
                app->widgetHovered = true;
                RenderWidget(app);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            app->widgetTracking = false;
            if (app->widgetHovered || app->widgetPressed) {
                app->widgetHovered = false;
                app->widgetPressed = false;
                RenderWidget(app);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (app->menuVisible) CloseMenu(app, false);
            if (GetKeyState(VK_MENU) < 0) {
                app->draggingOffset = true;
                GetCursorPos(&app->dragStartScreen);
                app->dragStartOffsetDip = app->lastPlacement.appliedOffsetXDip;
                SetCapture(hwnd);
            } else {
                app->widgetPressed = true;
                RenderWidget(app);
            }
            return 0;

        case WM_LBUTTONUP:
            if (app->draggingOffset) {
                app->draggingOffset = false;
                ReleaseCapture();
                SaveConfig(app->cfg);
            } else if (app->widgetPressed) {
                app->widgetPressed = false;
                RenderWidget(app);
                ToggleFlyout(app);
            }
            return 0;

        case WM_RBUTTONUP: {
            POINT pt{};
            GetCursorPos(&pt);
            ShowContextMenu(app, pt);
            return 0;
        }

        case WM_DESTROY:
            if (app->hwndWidget == hwnd) {
                app->renderer.ResetWidgetTarget();
                app->hwndWidget = nullptr;
                app->widgetVisible = false;
                app->widgetHovered = false;
                app->widgetTracking = false;
                app->widgetPressed = false;
                app->draggingOffset = false;
            }
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
