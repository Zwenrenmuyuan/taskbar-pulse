#include "app/widget_window.hpp"
#include "app/flyout_window.hpp"
#include "app/menu_window.hpp"
#include "app/tray_icon.hpp"
#include <algorithm>
#include <cmath>

namespace {

std::vector<HWND> CollectOwnWindows(const AppState* app) {
    std::vector<HWND> ownWindows;
    if (!app) return ownWindows;
    ownWindows.reserve(app->widgets.size() + 3);
    for (const auto& w : app->widgets) {
        if (w.hwnd) ownWindows.push_back(w.hwnd);
    }
    if (app->hwndFlyout) ownWindows.push_back(app->hwndFlyout);
    if (app->hwndMenu) ownWindows.push_back(app->hwndMenu);
    if (app->hwndSubmenu) ownWindows.push_back(app->hwndSubmenu);
    return ownWindows;
}

} // namespace

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
        app->history,
        app->goldPrice.Snapshot(),
        app->anim,
        w.hoverProgress,
        w.pressProgress);
}

void DestroyAllWidgets(AppState* app) {
    if (!app) return;
    std::vector<HWND> hwnds;
    hwnds.reserve(app->widgets.size());
    for (auto& w : app->widgets) {
        if (w.hwnd) {
            app->renderer.ResetWidgetTarget(w.hwnd);
            hwnds.push_back(w.hwnd);
        }
    }
    app->widgets.clear();
    app->flyoutAnchorWidget = nullptr;
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

    std::vector<HWND> ownWindows = CollectOwnWindows(app);
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
        TaskbarPlacement prevP = w->lastPlacement;

        bool changedSizeOrDpiOrY = !prevP.valid ||
            p.ptWidgetClient.y != prevP.ptWidgetClient.y ||
            p.szWidget.cx != prevP.szWidget.cx ||
            p.szWidget.cy != prevP.szWidget.cy ||
            p.dpi != prevP.dpi;
        bool changedX = !prevP.valid || p.ptWidgetClient.x != prevP.ptWidgetClient.x;

        if (!p.valid || !p.shouldShow) {
            w->lastPlacement = p;
            if (w->visible) {
                w->visible = false;
                w->hasAnimX = false;
                ShowWindow(w->hwnd, SW_HIDE);
            }
            continue;
        }

        const float targetX = static_cast<float>(p.ptWidgetClient.x);
        const bool movingX = w->hasAnimX && w->animX != targetX;
        if (!prevVis || changedSizeOrDpiOrY || changedX || movingX) {
            // Check the actual window, since layout can change while a glide
            // is still in progress toward the same target.
            RECT current{};
            const bool canGlideX = prevVis && w->hasAnimX && !w->draggingOffset && !changedSizeOrDpiOrY &&
                GetWindowRect(w->hwnd, &current) && std::abs(p.ptWidget.x - current.left) <= 900 &&
                current.left >= p.freeSpanPx.left && current.right <= p.freeSpanPx.right;
            if (canGlideX) {
                RequestAnimationFrame(app);
            } else {
                if (!SetWindowPos(w->hwnd, HWND_TOP, p.ptWidgetClient.x, p.ptWidgetClient.y,
                        p.szWidget.cx, p.szWidget.cy, SWP_NOACTIVATE | SWP_NOREDRAW)) continue;
                w->animX = targetX;
                w->hasAnimX = true;
            }
        }
        w->lastPlacement = p;

        if (!prevVis || changedSizeOrDpiOrY || forceRender || resumedFromFullscreen) {
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
                if (!GetCursorPos(&pt)) return 0;
                float scale = static_cast<float>(w->lastPlacement.dpi ? w->lastPlacement.dpi : 96) / 96.0f;
                // Keep the mouse-down origin while snapping to a free gap so
                // small moves can accumulate across occupied taskbar space.
                int deltaDip = static_cast<int>(std::lround(static_cast<float>(pt.x - w->dragStartScreen.x) / scale));
                int desiredOffset = w->dragStartOffsetDip + deltaDip;

                TaskbarPlacement p = app->host.ComputePlacementForTaskbar(
                    w->dragTaskbar, w->dragDim.widthDip, w->dragDim.heightDip, desiredOffset, w->dragOwnWindows);
                if (p.valid && p.shouldShow) {
                    if (p.ptWidgetClient.x != w->lastPlacement.ptWidgetClient.x ||
                        p.ptWidgetClient.y != w->lastPlacement.ptWidgetClient.y) {
                        if (!SetWindowPos(w->hwnd, nullptr, p.ptWidgetClient.x, p.ptWidgetClient.y, 0, 0,
                            SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOREDRAW)) return 0;
                    }
                    w->lastPlacement = p;
                    w->animX = static_cast<float>(p.ptWidgetClient.x);
                    w->hasAnimX = true;
                    SetMonitorOffsetDip(app->cfg, w->isPrimary, w->deviceName, p.appliedOffsetXDip);
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
                KickInteractiveAnimation(app, hwnd);
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
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            WidgetInstance* w = FindWidget(app, hwnd);
            if (!w) return 0;
            if (app->menuVisible) CloseMenu(app, false, true);
            if (GetKeyState(VK_MENU) < 0) {
                if (!GetCursorPos(&w->dragStartScreen)) return 0;
                w->draggingOffset = true;
                w->dragStartOffsetDip = w->lastPlacement.appliedOffsetXDip;
                w->dragDim = app->renderer.MeasureWidget(app->cfg);
                w->dragOwnWindows = CollectOwnWindows(app);
                w->dragTaskbar = {};
                for (const auto& tb : app->host.Taskbars()) {
                    if (tb.hwndTaskbar == w->hwndTaskbar) {
                        w->dragTaskbar = tb;
                        break;
                    }
                }
                if (!w->dragTaskbar.hwndTaskbar) {
                    w->dragTaskbar.hwndTaskbar = w->hwndTaskbar;
                    w->dragTaskbar.isPrimary = w->isPrimary;
                    w->dragTaskbar.deviceName = w->deviceName;
                }
                SetCapture(hwnd);
            } else {
                w->pressed = true;
                KickInteractiveAnimation(app, hwnd);
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
                SyncWidgetPlacement(app, false);
            } else if (w->pressed) {
                w->pressed = false;
                KickInteractiveAnimation(app, hwnd);
                ToggleFlyout(app, hwnd);
            }
            return 0;
        }

        case WM_CAPTURECHANGED: {
            WidgetInstance* w = FindWidget(app, hwnd);
            if (w && w->draggingOffset) {
                w->draggingOffset = false;
                SaveConfig(app->cfg);
                SyncWidgetPlacement(app, false);
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
                w->hasAnimX = false;
            }
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
