#include "app/widget_window.hpp"
#include "app/flyout_window.hpp"
#include "app/menu_window.hpp"
#include "app/tray_icon.hpp"
#include <algorithm>
#include <cmath>

WidgetInstance* FindWidget(AppState* app, HWND hwnd) {
    if (!app || !hwnd) return nullptr;
    for (auto& w : app->widgets) {
        if (w.hwnd == hwnd) return &w;
    }
    return nullptr;
}

const WidgetInstance* FindPreferredWidget(const AppState* app, HWND preferredHwnd) {
    if (!app || app->widgets.empty()) return nullptr;
    if (preferredHwnd) {
        for (const auto& w : app->widgets) {
            if (w.hwnd == preferredHwnd && w.lastPlacement.valid) return &w;
        }
    }
    for (const auto& w : app->widgets) {
        if (w.isPrimary && w.visible && w.lastPlacement.valid) return &w;
    }
    for (const auto& w : app->widgets) {
        if (w.visible && w.lastPlacement.valid) return &w;
    }
    for (const auto& w : app->widgets) {
        if (w.lastPlacement.valid) return &w;
    }
    return nullptr;
}

void RenderWidget(AppState* app, WidgetInstance& w) {
    if (!w.hwnd || !w.visible || !w.lastPlacement.shouldShow) return;
    app->renderer.RenderWidgetToWindow(
        w.hwnd,
        w.lastPlacement.ptWidget,
        w.lastPlacement.dpi,
        app->cfg,
        app->snap,
        app->sensors.History(),
        app->goldPrice.Snapshot(),
        w.hovered,
        w.pressed);
}

void DestroyAllWidgets(AppState* app) {
    if (!app) return;
    std::vector<HWND> hwnds;
    hwnds.reserve(app->widgets.size());
    for (auto& w : app->widgets) {
        if (w.hwnd) hwnds.push_back(w.hwnd);
    }
    app->widgets.clear();
    app->flyoutAnchorWidget = nullptr;
    app->renderer.ResetWidgetTarget(nullptr);
    for (HWND h : hwnds) {
        if (IsWindow(h)) DestroyWindow(h);
    }
}

void SyncWidgetPlacement(AppState* app, bool forceRender) {
    if (!app) return;
    app->host.RefreshHandles();
    std::vector<TaskbarInfo> targets = app->host.ResolveTargetTaskbars(app->cfg);

    auto hasTarget = [&](HWND tbHwnd) {
        for (const auto& tb : targets) {
            if (tb.hwndTaskbar == tbHwnd) return true;
        }
        return false;
    };

    // Remove widgets whose taskbar disappeared, changed mode, or lost parent.
    std::vector<HWND> toDestroy;
    app->widgets.erase(
        std::remove_if(app->widgets.begin(), app->widgets.end(), [&](const WidgetInstance& w) {
            bool keep = w.hwnd && IsWindow(w.hwnd) &&
                        w.hwndTaskbar && IsWindow(w.hwndTaskbar) &&
                        GetParent(w.hwnd) == w.hwndTaskbar &&
                        hasTarget(w.hwndTaskbar);
            if (!keep) {
                if (w.hwnd) toDestroy.push_back(w.hwnd);
                return true;
            }
            return false;
        }),
        app->widgets.end());

    for (HWND h : toDestroy) {
        app->renderer.ResetWidgetTarget(h);
        if (app->flyoutAnchorWidget == h) app->flyoutAnchorWidget = nullptr;
        if (IsWindow(h)) DestroyWindow(h);
    }

    // Create widgets for newly targeted taskbars and sync metadata.
    for (const auto& tb : targets) {
        if (!tb.hwndTaskbar || !IsWindow(tb.hwndTaskbar)) continue;
        WidgetInstance* existing = nullptr;
        for (auto& w : app->widgets) {
            if (w.hwndTaskbar == tb.hwndTaskbar) {
                existing = &w;
                break;
            }
        }
        if (existing) {
            existing->isPrimary = tb.isPrimary;
            existing->deviceName = tb.deviceName;
            continue;
        }

        HWND hwnd = CreateWindowExW(
            WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE,
            kWidgetClass, L"TaskbarMonitorWidget",
            WS_CHILD | WS_CLIPSIBLINGS,
            0, 0, 10, 10, tb.hwndTaskbar, nullptr, app->hInst, app);
        if (!hwnd) continue;

        WidgetInstance inst;
        inst.hwnd = hwnd;
        inst.hwndTaskbar = tb.hwndTaskbar;
        inst.isPrimary = tb.isPrimary;
        inst.deviceName = tb.deviceName;
        app->widgets.push_back(std::move(inst));
    }

    std::vector<HWND> ownWindows;
    ownWindows.reserve(app->widgets.size() + 3);
    for (const auto& w : app->widgets) {
        if (w.hwnd) ownWindows.push_back(w.hwnd);
    }
    if (app->hwndFlyout) ownWindows.push_back(app->hwndFlyout);
    if (app->hwndMenu) ownWindows.push_back(app->hwndMenu);
    if (app->hwndSubmenu) ownWindows.push_back(app->hwndSubmenu);

    WidgetDimensions dim = app->renderer.MeasureWidget(app->cfg);
    bool anyResumedFromFullscreen = false;

    for (const auto& tb : targets) {
        WidgetInstance* w = nullptr;
        for (auto& item : app->widgets) {
            if (item.hwndTaskbar == tb.hwndTaskbar) {
                w = &item;
                break;
            }
        }
        if (!w || !w->hwnd) continue;

        int offsetDip = GetMonitorOffsetDip(app->cfg, tb.isPrimary, tb.deviceName);
        TaskbarPlacement p = app->host.ComputePlacementForTaskbar(
            tb, dim.widthDip, dim.heightDip, offsetDip, ownWindows);

        if (p.occludedByFullscreen) {
            // Keep an already-visible WS_CHILD widget attached and visible under
            // the fullscreen window so transient overlays (e.g. Snipaste) never
            // expose a blank frame on dismiss; pause rendering and UIA updates
            // on this monitor until the fullscreen window leaves it.
            w->fullscreenPaused = true;
            continue;
        }

        bool resumedFromFullscreen = w->fullscreenPaused;
        w->fullscreenPaused = false;
        if (resumedFromFullscreen) anyResumedFromFullscreen = true;

        bool prevVis = w->visible;
        bool changedPosOrDpi = !w->lastPlacement.valid ||
            p.ptWidgetClient.x != w->lastPlacement.ptWidgetClient.x ||
            p.ptWidgetClient.y != w->lastPlacement.ptWidgetClient.y ||
            p.szWidget.cx != w->lastPlacement.szWidget.cx ||
            p.szWidget.cy != w->lastPlacement.szWidget.cy ||
            p.dpi != w->lastPlacement.dpi;

        w->lastPlacement = p;

        if (!p.valid || !p.shouldShow) {
            if (w->visible) {
                w->visible = false;
                ShowWindow(w->hwnd, SW_HIDE);
            }
            continue;
        }

        bool needResizeOrShow = !prevVis || changedPosOrDpi;
        if (needResizeOrShow) {
            SetWindowPos(w->hwnd, HWND_TOP,
                p.ptWidgetClient.x, p.ptWidgetClient.y,
                p.szWidget.cx, p.szWidget.cy,
                SWP_NOACTIVATE | SWP_NOREDRAW);
        }

        if (needResizeOrShow || forceRender || resumedFromFullscreen) {
            w->visible = true;
            RenderWidget(app, *w);
        }

        if (!prevVis) {
            ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
        }

        app->host.EnsureAboveTaskbar(tb.hwndTaskbar, w->hwnd, app->hwndFlyout);
    }

    if (anyResumedFromFullscreen) {
        UpdateTrayIcon(app, false);
    }
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
            WidgetInstance* w = FindWidget(app, hwnd);
            if (!w) return 0;
            if (w->draggingOffset) {
                POINT pt{};
                GetCursorPos(&pt);
                float scale = static_cast<float>(w->lastPlacement.dpi ? w->lastPlacement.dpi : 96) / 96.0f;
                int deltaDip = static_cast<int>(std::lround(static_cast<float>(pt.x - w->dragStartScreen.x) / scale));
                int desiredOffset = w->dragStartOffsetDip + deltaDip;
                bool isPrimary = w->isPrimary;
                std::wstring deviceName = w->deviceName;
                SetMonitorOffsetDip(app->cfg, isPrimary, deviceName, desiredOffset);
                SyncWidgetPlacement(app, true);
                w = FindWidget(app, hwnd);
                if (w && w->lastPlacement.shouldShow &&
                    desiredOffset != w->lastPlacement.appliedOffsetXDip) {
                    SetMonitorOffsetDip(app->cfg, w->isPrimary, w->deviceName, w->lastPlacement.appliedOffsetXDip);
                    w->dragStartOffsetDip = w->lastPlacement.appliedOffsetXDip;
                    w->dragStartScreen = pt;
                }
                return 0;
            }
            if (!w->tracking) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                w->tracking = true;
            }
            if (!w->hovered) {
                w->hovered = true;
                RenderWidget(app, *w);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            WidgetInstance* w = FindWidget(app, hwnd);
            if (!w) return 0;
            w->tracking = false;
            if (w->hovered || w->pressed) {
                w->hovered = false;
                w->pressed = false;
                RenderWidget(app, *w);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            WidgetInstance* w = FindWidget(app, hwnd);
            if (!w) return 0;
            if (app->menuVisible) CloseMenu(app, false);
            if (GetKeyState(VK_MENU) < 0) {
                w->draggingOffset = true;
                GetCursorPos(&w->dragStartScreen);
                w->dragStartOffsetDip = w->lastPlacement.appliedOffsetXDip;
                SetCapture(hwnd);
            } else {
                w->pressed = true;
                RenderWidget(app, *w);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            WidgetInstance* w = FindWidget(app, hwnd);
            if (!w) return 0;
            if (w->draggingOffset) {
                w->draggingOffset = false;
                ReleaseCapture();
                SaveConfig(app->cfg);
            } else if (w->pressed) {
                w->pressed = false;
                RenderWidget(app, *w);
                ToggleFlyout(app, hwnd);
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt{};
            GetCursorPos(&pt);
            ShowContextMenu(app, pt, false, hwnd);
            return 0;
        }

        case WM_DESTROY:
            app->renderer.ResetWidgetTarget(hwnd);
            if (app->flyoutAnchorWidget == hwnd) app->flyoutAnchorWidget = nullptr;
            if (WidgetInstance* w = FindWidget(app, hwnd)) {
                w->hwnd = nullptr;
                w->visible = false;
                w->fullscreenPaused = false;
                w->hovered = false;
                w->tracking = false;
                w->pressed = false;
                w->draggingOffset = false;
            }
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
