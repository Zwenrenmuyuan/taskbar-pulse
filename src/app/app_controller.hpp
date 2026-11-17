#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/config.hpp"
#include "collectors/gold_price.hpp"
#include "collectors/sensors.hpp"
#include "render/render_engine.hpp"
#include "taskbar/taskbar_host.hpp"
#include <vector>

inline constexpr wchar_t kHostClass[] = L"Win11TaskbarMonitor_Host";
inline constexpr wchar_t kWidgetClass[] = L"Win11TaskbarMonitor_Widget";
inline constexpr wchar_t kFlyoutClass[] = L"Win11TaskbarMonitor_Flyout";
inline constexpr wchar_t kMenuClass[] = L"Win11TaskbarMonitor_Menu";
inline constexpr wchar_t kSubmenuClass[] = L"Win11TaskbarMonitor_Submenu";

inline constexpr UINT_PTR kTimerPollId = 1;
inline constexpr UINT_PTR kTimerGuardId = 2;
inline constexpr UINT_PTR kTimerGoldId = 3;
inline constexpr UINT_PTR kTimerSubOpenId = 10;
inline constexpr UINT_PTR kTimerSubCloseId = 11;
inline constexpr UINT kTrayUid = 1;
inline constexpr UINT kMsgTray = WM_APP + 10;
inline constexpr UINT kMsgReposition = WM_APP + 11;

inline constexpr UINT kSubStyle = 9001;
inline constexpr UINT kSubTheme = 9002;
inline constexpr UINT kSubRow2 = 9003;
inline constexpr UINT kSubRate = 9004;

struct AppState {
    HINSTANCE hInst = nullptr;
    HICON hIcon = nullptr;
    HWND hwndHost = nullptr;
    HWND hwndWidget = nullptr;
    HWND hwndFlyout = nullptr;
    HWND hwndMenu = nullptr;
    HWND hwndSubmenu = nullptr;
    UINT msgTaskbarCreated = 0;

    AppConfig cfg{};
    SystemSensors sensors;
    SystemSnapshot snap{};
    GoldPrice goldPrice;
    RenderEngine renderer;
    TaskbarHost host;

    TaskbarPlacement lastPlacement{};
    bool widgetVisible = false;
    bool widgetHovered = false;
    bool widgetTracking = false;
    bool widgetPressed = false;

    bool draggingOffset = false;
    POINT dragStartScreen{};
    int dragStartOffsetDip = 0;

    bool flyoutVisible = false;
    bool suppressFlyoutDeactivate = false;
    ULONGLONG lastFlyoutClosedTick = 0;
    int flyoutHoverBtn = -1;
    bool flyoutTracking = false;
    RECT flyoutActionRects[4]{};

    bool menuVisible = false;
    bool menuTracking = false;
    bool submenuVisible = false;
    bool submenuTracking = false;
    UINT menuDpi = 96;
    POINT menuWinPos{};
    POINT submenuWinPos{};
    FluentMenuSpec menuSpec{};
    FluentMenuSpec submenuSpec{};
    std::vector<RECT> menuItemRects;
    std::vector<RECT> submenuItemRects;
    int menuHoverIdx = -1;
    int submenuHoverIdx = -1;
    int openSubIdx = -1;
    int pendingSubIdx = -1;

    bool trayAdded = false;
    bool repositionPending = false;
};

inline AppState* GetApp(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void ApplyRefreshTimer(AppState* app);
void ApplyGoldTimer(AppState* app);
void ExecuteMenuCommand(AppState* app, UINT cmd);
int RunApplication(HINSTANCE hInstance);
