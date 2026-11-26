#include "app/flyout_window.hpp"
#include "app/widget_window.hpp"
#include "app/menu_window.hpp"
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

FlyoutControl HitTestFlyoutControl(const FlyoutHitMap& map, POINT pt) {
    constexpr int kCount = static_cast<int>(FlyoutControl::Count);
    for (int i = 0; i < kCount; ++i) {
        if (!IsRectEmpty(&map.controls[i]) && PtInRect(&map.controls[i], pt)) {
            return static_cast<FlyoutControl>(i);
        }
    }
    return FlyoutControl::None;
}

int HitTestFlyoutRow(const FlyoutHitMap& map, POINT pt) {
    for (int i = 0; i < 4; ++i) {
        if (!IsRectEmpty(&map.overviewRows[i]) && PtInRect(&map.overviewRows[i], pt)) {
            return i;
        }
    }
    return -1;
}

int HitTestSheetItem(const FlyoutHitMap& map, POINT pt) {
    if (IsRectEmpty(&map.sheetListViewport) || !PtInRect(&map.sheetListViewport, pt)) {
        return -1;
    }
    for (size_t i = 0; i < map.sheetItemRects.size(); ++i) {
        if (!IsRectEmpty(&map.sheetItemRects[i]) && PtInRect(&map.sheetItemRects[i], pt)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::wstring> CollectSecondaryDevices(const AppState* app) {
    std::vector<std::wstring> sec;
    if (!app) return sec;
    for (const auto& tb : app->host.Taskbars()) {
        if (!tb.isPrimary && !tb.deviceName.empty()) {
            sec.push_back(tb.deviceName);
        }
    }
    return sec;
}

std::vector<FlyoutDisplayItem> BuildDisplayItemsList(const AppState* app) {
    std::vector<FlyoutDisplayItem> items;
    if (!app) return items;
    const auto& tbs = app->host.Taskbars();
    items.reserve(tbs.size());
    unsigned secNo = 0;
    for (size_t i = 0; i < tbs.size(); ++i) {
        const auto& tb = tbs[i];
        FlyoutDisplayItem it{};
        it.deviceName = tb.deviceName;
        it.isPrimary = tb.isPrimary;

        if (!tb.friendlyName.empty()) {
            it.title = tb.friendlyName;
        } else {
            wchar_t tbuf[32]{};
            _snwprintf_s(tbuf, _countof(tbuf), _TRUNCATE, L"显示器 %u", static_cast<unsigned>(i + 1));
            it.title = tbuf;
        }

        if (tb.isPrimary) {
            it.badge = L"主屏";
        } else {
            wchar_t bbuf[16]{};
            _snwprintf_s(bbuf, _countof(bbuf), _TRUNCATE, L"副%u", ++secNo);
            it.badge = bbuf;
        }

        int mw = tb.rcMonitor.right - tb.rcMonitor.left;
        int mh = tb.rcMonitor.bottom - tb.rcMonitor.top;
        int pct = (tb.dpi > 0) ? static_cast<int>(std::lround(static_cast<double>(tb.dpi) * 100.0 / 96.0)) : 100;
        if (mw > 0 && mh > 0) {
            wchar_t sbuf[48]{};
            _snwprintf_s(sbuf, _countof(sbuf), _TRUNCATE, L"%d × %d · %d%%", mw, mh, pct);
            it.subtitle = sbuf;
        }

        if (app->cfg.displayMode == DisplayTargetMode::Primary && tb.isPrimary) {
            it.selected = true;
        } else if (app->cfg.displayMode == DisplayTargetMode::Specific &&
                   !tb.deviceName.empty() &&
                   _wcsicmp(tb.deviceName.c_str(), app->cfg.displayDevice.c_str()) == 0) {
            it.selected = true;
        }
        items.push_back(std::move(it));
    }
    return items;
}

FlyoutDisplayOptions BuildFlyoutDisplayOptions(AppState* app) {
    FlyoutDisplayOptions opts{};
    opts.count = 2;
    opts.labels[0] = L"全部";
    opts.labels[1] = L"主屏";
    opts.activeIndex = (app && app->cfg.displayMode == DisplayTargetMode::Primary) ? 1 : 0;
    if (!app) return opts;

    const auto& tbs = app->host.Taskbars();
    std::vector<std::wstring> secondaries = CollectSecondaryDevices(app);

    if (tbs.size() >= 4 && secondaries.size() >= 3) {
        // 4+ monitors -> crisp 3-segment pill: [ 全部 | 主屏 | 指定 ▾ / 副N ▾ ] + Bottom Modal Sheet
        opts.usesSheet = true;
        opts.count = 3;
        opts.labels[2] = L"指定 ▾";
        opts.items = app->flyoutDisplayItems.empty() ? BuildDisplayItemsList(app) : app->flyoutDisplayItems;
        // Keep selection badges in sync with latest cfg even if cached
        for (auto& it : opts.items) {
            it.selected = (app->cfg.displayMode == DisplayTargetMode::Primary && it.isPrimary) ||
                          (app->cfg.displayMode == DisplayTargetMode::Specific &&
                           !it.deviceName.empty() &&
                           _wcsicmp(it.deviceName.c_str(), app->cfg.displayDevice.c_str()) == 0);
        }

        if (app->cfg.displayMode == DisplayTargetMode::All) {
            opts.activeIndex = 0;
        } else if (app->cfg.displayMode == DisplayTargetMode::Primary) {
            opts.activeIndex = 1;
        } else {
            bool matchedPrimary = false;
            for (const auto& tb : tbs) {
                if (tb.isPrimary && !tb.deviceName.empty() &&
                    _wcsicmp(tb.deviceName.c_str(), app->cfg.displayDevice.c_str()) == 0) {
                    opts.activeIndex = 1;
                    matchedPrimary = true;
                    break;
                }
            }
            if (!matchedPrimary) {
                opts.activeIndex = 2;
                for (size_t i = 0; i < secondaries.size(); ++i) {
                    if (_wcsicmp(secondaries[i].c_str(), app->cfg.displayDevice.c_str()) == 0) {
                        wchar_t b[20]{};
                        _snwprintf_s(b, _countof(b), _TRUNCATE, L"副%u ▾", static_cast<unsigned>(i + 1));
                        opts.labels[2] = b;
                        break;
                    }
                }
            }
        }
        return opts;
    }

    // 1-3 monitors: direct 1-click segmented pills
    if (secondaries.size() == 1) {
        opts.count = 3;
        opts.labels[2] = L"副屏";
    } else if (secondaries.size() == 2) {
        opts.count = 4;
        opts.labels[2] = L"副1";
        opts.labels[3] = L"副2";
    }

    if (app->cfg.displayMode == DisplayTargetMode::All) {
        opts.activeIndex = 0;
    } else if (app->cfg.displayMode == DisplayTargetMode::Primary) {
        opts.activeIndex = 1;
    } else {
        bool matched = false;
        for (const auto& tb : tbs) {
            if (tb.isPrimary && !tb.deviceName.empty() &&
                _wcsicmp(tb.deviceName.c_str(), app->cfg.displayDevice.c_str()) == 0) {
                opts.activeIndex = 1;
                matched = true;
                break;
            }
        }
        if (!matched) {
            for (size_t i = 0; i < secondaries.size() && i < 2; ++i) {
                if (_wcsicmp(secondaries[i].c_str(), app->cfg.displayDevice.c_str()) == 0) {
                    opts.activeIndex = static_cast<int>(2 + i);
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) opts.activeIndex = 0;
    }
    return opts;
}

void OpenDisplaySheet(AppState* app, HWND hwnd) {
    if (!app) return;
    app->host.RefreshHandles(true);
    app->flyoutDisplayItems = BuildDisplayItemsList(app);
    app->flyoutSheetOpen = true;
    app->flyoutSheetHoverIdx = -1;
    app->flyoutHoverCtrl = FlyoutControl::None;
    app->flyoutPressedCtrl = FlyoutControl::None;
    app->flyoutSheetScrollTarget = 0.0f;
    app->flyoutAnim.sheetScrollY = 0.0f;
    // Scroll selected monitor neatly into view if far down the list
    for (size_t i = 0; i < app->flyoutDisplayItems.size(); ++i) {
        if (app->flyoutDisplayItems[i].selected && i >= 4) {
            app->flyoutSheetScrollTarget = static_cast<float>(i - 2) * 38.0f;
            app->flyoutAnim.sheetScrollY = app->flyoutSheetScrollTarget;
            break;
        }
    }
    KickInteractiveAnimation(app, hwnd);
}

void CloseDisplaySheet(AppState* app, HWND hwnd) {
    if (!app || !app->flyoutSheetOpen) return;
    app->flyoutSheetOpen = false;
    app->flyoutSheetHoverIdx = -1;
    app->flyoutHoverCtrl = FlyoutControl::None;
    app->flyoutPressedCtrl = FlyoutControl::None;
    KickInteractiveAnimation(app, hwnd);
}

void SelectDisplaySegment(AppState* app, HWND hwnd, int segIdx) {
    if (!app) return;
    app->host.RefreshHandles(false);
    const auto& tbs = app->host.Taskbars();
    std::vector<std::wstring> secondaries = CollectSecondaryDevices(app);

    if (tbs.size() >= 4 && secondaries.size() >= 3 && segIdx == 2) {
        OpenDisplaySheet(app, hwnd);
        return;
    }

    if (segIdx == 0) {
        app->cfg.displayMode = DisplayTargetMode::All;
        app->cfg.displayDevice.clear();
    } else if (segIdx == 1) {
        app->cfg.displayMode = DisplayTargetMode::Primary;
        app->cfg.displayDevice.clear();
    } else if (segIdx == 2 && !secondaries.empty()) {
        app->cfg.displayMode = DisplayTargetMode::Specific;
        app->cfg.displayDevice = secondaries[0];
    } else if (segIdx == 3 && secondaries.size() >= 2) {
        app->cfg.displayMode = DisplayTargetMode::Specific;
        app->cfg.displayDevice = secondaries[1];
    }

    SyncWidgetPlacement(app, true);
    if (app->flyoutVisible) {
        RenderAndPositionFlyout(app);
    }
    SaveConfig(app->cfg);
}

void SelectSheetDisplayItem(AppState* app, HWND hwnd, int itemIdx) {
    if (!app || itemIdx < 0 || itemIdx >= static_cast<int>(app->flyoutDisplayItems.size())) return;
    const auto& it = app->flyoutDisplayItems[static_cast<size_t>(itemIdx)];
    if (it.isPrimary) {
        app->cfg.displayMode = DisplayTargetMode::Primary;
        app->cfg.displayDevice.clear();
    } else if (!it.deviceName.empty()) {
        app->cfg.displayMode = DisplayTargetMode::Specific;
        app->cfg.displayDevice = it.deviceName;
    }
    app->flyoutSheetOpen = false;
    app->flyoutSheetHoverIdx = -1;
    app->flyoutHoverCtrl = FlyoutControl::None;
    app->flyoutPressedCtrl = FlyoutControl::None;

    SyncWidgetPlacement(app, true);
    KickInteractiveAnimation(app, hwnd);
    SaveConfig(app->cfg);
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
    app->flyoutSheetOpen = false;
    app->flyoutSheetHoverIdx = -1;
    app->flyoutHoverRow = -1;
    app->flyoutHoverCtrl = FlyoutControl::None;
    app->flyoutPressedCtrl = FlyoutControl::None;
    app->flyoutTracking = false;
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

    int gap = static_cast<int>(std::lround(8.0f * scale));
    int fx = std::clamp(centerX - fwPx / 2, static_cast<int>(rcMon.left) + gap, static_cast<int>(rcMon.right) - fwPx - gap);
    int fy = taskbarAtTop
        ? (rcAnchor.bottom + gap)
        : (rcAnchor.top - fhPx - gap);
    fy = std::clamp(fy, static_cast<int>(rcMon.top) + gap, static_cast<int>(rcMon.bottom) - fhPx - gap);

    FlyoutDisplayOptions dispOpts = BuildFlyoutDisplayOptions(app);

    app->renderer.RenderFlyoutToWindow(
        app->hwndFlyout,
        POINT{ fx, fy },
        dpi,
        app->cfg,
        app->snap,
        app->history,
        app->goldPrice.Snapshot(),
        app->autoStartCached,
        dispOpts,
        app->anim,
        app->flyoutAnim,
        app->flyoutHoverCtrl,
        app->flyoutPressedCtrl,
        app->flyoutSheetHoverIdx,
        app->flyoutHitMap);
}

void ShowFlyout(AppState* app, HWND anchorWidget) {
    if (!app->hwndFlyout) return;
    bool wasVisible = app->flyoutVisible;
    KillTimer(app->hwndFlyout, kTimerFlyoutHideId);
    SetClickThrough(app->hwndFlyout, false);

    app->flyoutAnchorWidget = anchorWidget;
    app->flyoutVisible = true;
    app->flyoutSettingsPage = false;
    app->flyoutSheetOpen = false;
    app->flyoutSheetHoverIdx = -1;
    app->flyoutSheetScrollTarget = 0.0f;
    app->flyoutDisplayItems.clear();
    app->autoStartCached = IsAutoStartEnabled();
    app->flyoutHoverRow = -1;
    app->flyoutHoverCtrl = FlyoutControl::None;
    app->flyoutPressedCtrl = FlyoutControl::None;
    app->flyoutAnim = {};
    app->flyoutAnim.togglePos[0] = app->cfg.showMiniBars ? 1.0f : 0.0f;
    app->flyoutAnim.togglePos[1] = app->cfg.showGpuTemp ? 1.0f : 0.0f;
    app->flyoutAnim.togglePos[2] = app->cfg.showGoldPrice ? 1.0f : 0.0f;
    app->flyoutAnim.togglePos[3] = app->autoStartCached ? 1.0f : 0.0f;
    app->flyoutAnim.togglesInitialized = true;

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
                if (app->flyoutSheetOpen) {
                    CloseDisplaySheet(app, hwnd);
                } else if (app->flyoutSettingsPage) {
                    app->flyoutSettingsPage = false;
                    app->flyoutHoverCtrl = FlyoutControl::None;
                    app->flyoutPressedCtrl = FlyoutControl::None;
                    KickInteractiveAnimation(app, hwnd);
                } else {
                    HideFlyout(app, true);
                }
                return 0;
            }
            break;

        case WM_MOUSEWHEEL: {
            if (!app->flyoutVisible || !app->flyoutSheetOpen) return 0;
            if (app->flyoutHitMap.sheetMaxScrollDip <= 0.5f) return 0;
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float stepDip = -static_cast<float>(delta) / 120.0f * 52.0f;
            app->flyoutSheetScrollTarget = std::clamp(
                app->flyoutSheetScrollTarget + stepDip, 0.0f, app->flyoutHitMap.sheetMaxScrollDip);
            KickInteractiveAnimation(app, hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!app->flyoutVisible) return 0;
            if (!app->flyoutTracking) {
                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                app->flyoutTracking = true;
            }
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            FlyoutControl hitCtrl = HitTestFlyoutControl(app->flyoutHitMap, pt);
            int hitRow = app->flyoutSheetOpen ? -1 : HitTestFlyoutRow(app->flyoutHitMap, pt);
            int hitSheet = app->flyoutSheetOpen ? HitTestSheetItem(app->flyoutHitMap, pt) : -1;

            bool clickable = (hitCtrl != FlyoutControl::None) || (hitSheet >= 0);
            SetCursor(LoadCursorW(nullptr, clickable ? IDC_HAND : IDC_ARROW));
            if (hitCtrl != app->flyoutHoverCtrl || hitRow != app->flyoutHoverRow || hitSheet != app->flyoutSheetHoverIdx) {
                app->flyoutHoverCtrl = hitCtrl;
                app->flyoutHoverRow = hitRow;
                app->flyoutSheetHoverIdx = hitSheet;
                if (app->flyoutPressedCtrl != FlyoutControl::None && app->flyoutPressedCtrl != hitCtrl) {
                    app->flyoutPressedCtrl = FlyoutControl::None;
                }
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            app->flyoutTracking = false;
            if (app->flyoutHoverCtrl != FlyoutControl::None ||
                app->flyoutPressedCtrl != FlyoutControl::None ||
                app->flyoutHoverRow != -1 ||
                app->flyoutSheetHoverIdx != -1) {
                app->flyoutHoverCtrl = FlyoutControl::None;
                app->flyoutPressedCtrl = FlyoutControl::None;
                app->flyoutHoverRow = -1;
                app->flyoutSheetHoverIdx = -1;
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            if (!app->flyoutVisible) return 0;
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            FlyoutControl hit = HitTestFlyoutControl(app->flyoutHitMap, pt);
            if (hit != app->flyoutPressedCtrl) {
                app->flyoutPressedCtrl = hit;
                KickInteractiveAnimation(app, hwnd);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!app->flyoutVisible) return 0;
            POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            FlyoutControl hit = HitTestFlyoutControl(app->flyoutHitMap, pt);

            if (app->flyoutPressedCtrl != FlyoutControl::None) {
                app->flyoutPressedCtrl = FlyoutControl::None;
                KickInteractiveAnimation(app, hwnd);
            }

            if (app->flyoutSheetOpen) {
                if (hit == FlyoutControl::SheetCloseBtn) {
                    CloseDisplaySheet(app, hwnd);
                    return 0;
                }
                int hitItem = HitTestSheetItem(app->flyoutHitMap, pt);
                if (hitItem >= 0) {
                    SelectSheetDisplayItem(app, hwnd, hitItem);
                    return 0;
                }
                if (IsRectEmpty(&app->flyoutHitMap.sheetCard) || !PtInRect(&app->flyoutHitMap.sheetCard, pt)) {
                    CloseDisplaySheet(app, hwnd);
                }
                return 0;
            }

            if (hit == FlyoutControl::None) return 0;
            if (app->menuVisible) CloseMenu(app, false, true);

            switch (hit) {
                case FlyoutControl::GearBtn:
                    app->flyoutSettingsPage = true;
                    app->flyoutSheetOpen = false;
                    app->flyoutHoverRow = -1;
                    app->flyoutHoverCtrl = FlyoutControl::None;
                    KickInteractiveAnimation(app, hwnd);
                    break;
                case FlyoutControl::BackBtn:
                    app->flyoutSettingsPage = false;
                    app->flyoutSheetOpen = false;
                    app->flyoutHoverCtrl = FlyoutControl::None;
                    KickInteractiveAnimation(app, hwnd);
                    break;
                case FlyoutControl::SegStyleMinimal:
                    ExecuteMenuCommand(app, IDM_STYLE_MINIMAL);
                    break;
                case FlyoutControl::SegStyleCapsule:
                    ExecuteMenuCommand(app, IDM_STYLE_CAPSULE);
                    break;
                case FlyoutControl::SegThemeAuto:
                    ExecuteMenuCommand(app, IDM_THEME_AUTO);
                    break;
                case FlyoutControl::SegThemeLight:
                    ExecuteMenuCommand(app, IDM_THEME_LIGHT);
                    break;
                case FlyoutControl::SegThemeDark:
                    ExecuteMenuCommand(app, IDM_THEME_DARK);
                    break;
                case FlyoutControl::SegRate500:
                    ExecuteMenuCommand(app, IDM_RATE_500);
                    break;
                case FlyoutControl::SegRate1000:
                    ExecuteMenuCommand(app, IDM_RATE_1000);
                    break;
                case FlyoutControl::SegRate2000:
                    ExecuteMenuCommand(app, IDM_RATE_2000);
                    break;
                case FlyoutControl::SegRow2Temp:
                    ExecuteMenuCommand(app, IDM_ROW2_CPU_TEMP);
                    break;
                case FlyoutControl::SegRow2Freq:
                    ExecuteMenuCommand(app, IDM_ROW2_CPU_FREQ);
                    break;
                case FlyoutControl::SegRow2Mem:
                    ExecuteMenuCommand(app, IDM_ROW2_MEM_PCT);
                    break;
                case FlyoutControl::SegRow2Hide:
                    ExecuteMenuCommand(app, IDM_ROW2_HIDE);
                    break;
                case FlyoutControl::SegDisp0:
                    SelectDisplaySegment(app, hwnd, 0);
                    break;
                case FlyoutControl::SegDisp1:
                    SelectDisplaySegment(app, hwnd, 1);
                    break;
                case FlyoutControl::SegDisp2:
                    SelectDisplaySegment(app, hwnd, 2);
                    break;
                case FlyoutControl::SegDisp3:
                    SelectDisplaySegment(app, hwnd, 3);
                    break;
                case FlyoutControl::ToggleMiniBars:
                    ExecuteMenuCommand(app, IDM_TOGGLE_BARS);
                    KickInteractiveAnimation(app, hwnd);
                    break;
                case FlyoutControl::ToggleGpuTemp:
                    ExecuteMenuCommand(app, IDM_TOGGLE_GPUTEMP);
                    KickInteractiveAnimation(app, hwnd);
                    break;
                case FlyoutControl::ToggleGold:
                    ExecuteMenuCommand(app, IDM_TOGGLE_GOLD);
                    KickInteractiveAnimation(app, hwnd);
                    break;
                case FlyoutControl::ToggleAutoStart:
                    ExecuteMenuCommand(app, IDM_TOGGLE_AUTOSTART);
                    KickInteractiveAnimation(app, hwnd);
                    break;
                case FlyoutControl::BtnResetPos:
                    ExecuteMenuCommand(app, IDM_RESET_OFFSET);
                    break;
                case FlyoutControl::BtnExit:
                    ExecuteMenuCommand(app, IDM_EXIT);
                    break;
                default:
                    break;
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
