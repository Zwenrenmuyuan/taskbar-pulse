#include "app/app_controller.hpp"
#include "app/tray_icon.hpp"
#include "app/widget_window.hpp"
#include "app/flyout_window.hpp"
#include "app/menu_window.hpp"
#include "resource.h"
#include <shellapi.h>

namespace {

void OnTaskbarChangedCallback(void* userData) {
    auto* app = static_cast<AppState*>(userData);
    if (app && app->hwndHost && !app->repositionPending) {
        app->repositionPending = true;
        PostMessageW(app->hwndHost, kMsgReposition, 0, 0);
    }
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = GetApp(hwnd);
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);

    if (app->msgTaskbarCreated != 0 && msg == app->msgTaskbarCreated) {
        app->trayAdded = false;
        UpdateTrayIcon(app, true);
        app->host.RefreshHandles();
        SyncWidgetPlacement(app, true);
        return 0;
    }

    switch (msg) {
        case WM_TIMER:
            if (wParam == kTimerPollId) {
                app->snap = app->sensors.Poll();
                bool anyActive = false;
                for (auto& w : app->widgets) {
                    if (!w.fullscreenPaused) {
                        RenderWidget(app, w);
                        anyActive = true;
                    }
                }
                if (anyActive || app->widgets.empty()) {
                    UpdateTrayIcon(app, false);
                }
                if (app->flyoutVisible) RenderAndPositionFlyout(app);
            } else if (wParam == kTimerGuardId) {
                SyncWidgetPlacement(app, false);
            } else if (wParam == kTimerGoldId) {
                app->goldPrice.Refresh();
            }
            return 0;

        case kMsgReposition:
            app->repositionPending = false;
            SyncWidgetPlacement(app, false);
            return 0;

        case WM_SETTINGCHANGE:
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (msg == WM_DISPLAYCHANGE) app->host.RefreshHandles(true);
            SyncWidgetPlacement(app, true);
            if (app->flyoutVisible) RenderAndPositionFlyout(app);
            return 0;

        case kMsgTray: {
            UINT ev = LOWORD(lParam);
            if (ev == WM_LBUTTONUP || ev == NIN_SELECT || ev == NIN_KEYSELECT) {
                ToggleFlyout(app);
            } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
                POINT pt{};
                GetCursorPos(&pt);
                ShowContextMenu(app, pt);
            }
            return 0;
        }

        case WM_DESTROY:
            CloseMenu(app, false);
            KillTimer(hwnd, kTimerPollId);
            KillTimer(hwnd, kTimerGuardId);
            KillTimer(hwnd, kTimerGoldId);
            app->host.DetachHook();
            RemoveTrayIcon(app);
            if (app->hwndSubmenu) {
                DestroyWindow(app->hwndSubmenu);
                app->hwndSubmenu = nullptr;
            }
            if (app->hwndMenu) {
                DestroyWindow(app->hwndMenu);
                app->hwndMenu = nullptr;
            }
            DestroyAllWidgets(app);
            if (app->hwndFlyout) {
                DestroyWindow(app->hwndFlyout);
                app->hwndFlyout = nullptr;
            }
            app->hwndHost = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void ApplyRefreshTimer(AppState* app) {
    if (!app->hwndHost) return;
    KillTimer(app->hwndHost, kTimerPollId);
    SetTimer(app->hwndHost, kTimerPollId, static_cast<UINT>(app->cfg.refreshMs), nullptr);
}

void ApplyGoldTimer(AppState* app) {
    KillTimer(app->hwndHost, kTimerGoldId);
    if (app->cfg.showGoldPrice) {
        app->goldPrice.Refresh();
        SetTimer(app->hwndHost, kTimerGoldId, 30000, nullptr);
    }
}

void ExecuteMenuCommand(AppState* app, UINT cmd) {
    if (cmd == 0) return;
    bool changed = false;
    switch (cmd) {
        case IDM_OPEN_FLYOUT:
            ToggleFlyout(app);
            break;
        case IDM_STYLE_CAPSULE:
            app->cfg.style = VisualStyle::Capsule;
            changed = true;
            break;
        case IDM_STYLE_MINIMAL:
            app->cfg.style = VisualStyle::Minimal;
            changed = true;
            break;
        case IDM_THEME_AUTO:
            app->cfg.theme = ThemeMode::Auto;
            changed = true;
            break;
        case IDM_THEME_LIGHT:
            app->cfg.theme = ThemeMode::Light;
            changed = true;
            break;
        case IDM_THEME_DARK:
            app->cfg.theme = ThemeMode::Dark;
            changed = true;
            break;
        case IDM_TOGGLE_BARS:
            app->cfg.showMiniBars = !app->cfg.showMiniBars;
            changed = true;
            break;
        case IDM_ROW2_CPU_FREQ:
            app->cfg.row2Col1 = Row2Col1Mode::CpuFreq;
            changed = true;
            break;
        case IDM_ROW2_MEM_PCT:
            app->cfg.row2Col1 = Row2Col1Mode::MemPct;
            changed = true;
            break;
        case IDM_ROW2_CPU_TEMP:
            app->cfg.row2Col1 = Row2Col1Mode::CpuTemp;
            changed = true;
            break;
        case IDM_ROW2_HIDE:
            app->cfg.row2Col1 = Row2Col1Mode::Hide;
            changed = true;
            break;
        case IDM_TOGGLE_GPUTEMP:
            app->cfg.showGpuTemp = !app->cfg.showGpuTemp;
            changed = true;
            break;
        case IDM_TOGGLE_GOLD:
            app->cfg.showGoldPrice = !app->cfg.showGoldPrice;
            ApplyGoldTimer(app);
            changed = true;
            break;
        case IDM_RATE_500:
            app->cfg.refreshMs = 500;
            ApplyRefreshTimer(app);
            changed = true;
            break;
        case IDM_RATE_1000:
            app->cfg.refreshMs = 1000;
            ApplyRefreshTimer(app);
            changed = true;
            break;
        case IDM_RATE_2000:
            app->cfg.refreshMs = 2000;
            ApplyRefreshTimer(app);
            changed = true;
            break;
        case IDM_RESET_OFFSET:
            ResetAllMonitorOffsets(app->cfg);
            changed = true;
            break;
        case IDM_DISPLAY_ALL:
            app->cfg.displayMode = DisplayTargetMode::All;
            app->cfg.displayDevice.clear();
            changed = true;
            break;
        case IDM_DISPLAY_PRIMARY:
            app->cfg.displayMode = DisplayTargetMode::Primary;
            app->cfg.displayDevice.clear();
            changed = true;
            break;
        case IDM_TOGGLE_AUTOSTART:
            SetAutoStartEnabled(!IsAutoStartEnabled());
            break;
        case IDM_EXIT:
            DestroyWindow(app->hwndHost);
            return;
        default:
            if (cmd >= IDM_DISPLAY_SPEC_BASE && cmd <= IDM_DISPLAY_SPEC_MAX) {
                size_t idx = static_cast<size_t>(cmd - IDM_DISPLAY_SPEC_BASE);
                if (idx < app->menuDisplayDevices.size()) {
                    app->cfg.displayMode = DisplayTargetMode::Specific;
                    app->cfg.displayDevice = app->menuDisplayDevices[idx];
                    changed = true;
                }
            }
            break;
    }

    if (changed) {
        SaveConfig(app->cfg);
        SyncWidgetPlacement(app, true);
        if (app->flyoutVisible) {
            RenderAndPositionFlyout(app);
        }
    }
}

int RunApplication(HINSTANCE hInstance) {
    AppState app;
    app.hInst = hInstance;
    int smIcon = GetSystemMetrics(SM_CXSMICON);
    app.hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, smIcon, smIcon, LR_DEFAULTCOLOR));
    if (!app.hIcon) app.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    app.msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    app.cfg = LoadConfig();

    int exitCode = 0;
    if (!app.renderer.Initialize()) {
        exitCode = 1;
    } else {
        app.sensors.Initialize();
        app.snap = app.sensors.Poll();

        WNDCLASSEXW wcH{};
        wcH.cbSize = sizeof(wcH);
        wcH.lpfnWndProc = HostWndProc;
        wcH.hInstance = hInstance;
        wcH.lpszClassName = kHostClass;
        RegisterClassExW(&wcH);

        WNDCLASSEXW wcW{};
        wcW.cbSize = sizeof(wcW);
        wcW.style = CS_HREDRAW | CS_VREDRAW;
        wcW.lpfnWndProc = WidgetWndProc;
        wcW.hInstance = hInstance;
        wcW.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcW.lpszClassName = kWidgetClass;
        RegisterClassExW(&wcW);

        WNDCLASSEXW wcF{};
        wcF.cbSize = sizeof(wcF);
        wcF.style = CS_HREDRAW | CS_VREDRAW;
        wcF.lpfnWndProc = FlyoutWndProc;
        wcF.hInstance = hInstance;
        wcF.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcF.lpszClassName = kFlyoutClass;
        RegisterClassExW(&wcF);

        WNDCLASSEXW wcM{};
        wcM.cbSize = sizeof(wcM);
        wcM.lpfnWndProc = MenuWndProc;
        wcM.hInstance = hInstance;
        wcM.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcM.lpszClassName = kMenuClass;
        RegisterClassExW(&wcM);

        WNDCLASSEXW wcS{};
        wcS.cbSize = sizeof(wcS);
        wcS.lpfnWndProc = SubmenuWndProc;
        wcS.hInstance = hInstance;
        wcS.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wcS.lpszClassName = kSubmenuClass;
        RegisterClassExW(&wcS);

        app.hwndHost = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kHostClass, L"TaskbarMonitorHost", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, hInstance, &app);

        app.hwndFlyout = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kFlyoutClass, L"TaskbarMonitorFlyout", WS_POPUP,
            0, 0, 10, 10, nullptr, nullptr, hInstance, &app);

        app.hwndMenu = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kMenuClass, L"TaskbarMonitorMenu", WS_POPUP,
            0, 0, 10, 10, nullptr, nullptr, hInstance, &app);

        app.hwndSubmenu = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kSubmenuClass, L"TaskbarMonitorSubmenu", WS_POPUP,
            0, 0, 10, 10, nullptr, nullptr, hInstance, &app);

        app.host.AttachHook(&OnTaskbarChangedCallback, &app);
        UpdateTrayIcon(&app, true);
        SyncWidgetPlacement(&app, true);

        ApplyRefreshTimer(&app);
        ApplyGoldTimer(&app);
        SetTimer(app.hwndHost, kTimerGuardId, 350, nullptr);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (app.hIcon) DestroyIcon(app.hIcon);
    return exitCode;
}
