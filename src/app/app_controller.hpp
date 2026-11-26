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
#include "render/animator.hpp"
#include "render/render_engine.hpp"
#include "taskbar/taskbar_host.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shellapi.h>
#include <string>
#include <thread>
#include <vector>

inline constexpr wchar_t kHostClass[] = L"Win11TaskbarMonitor_Host";
inline constexpr wchar_t kWidgetClass[] = L"Win11TaskbarMonitor_Widget";
inline constexpr wchar_t kFlyoutClass[] = L"Win11TaskbarMonitor_Flyout";
inline constexpr wchar_t kMenuClass[] = L"Win11TaskbarMonitor_Menu";
inline constexpr wchar_t kSubmenuClass[] = L"Win11TaskbarMonitor_Submenu";

inline constexpr UINT_PTR kTimerGuardId = 2;
inline constexpr UINT_PTR kTimerGoldId = 3;
inline constexpr UINT_PTR kTimerSubOpenId = 10;
inline constexpr UINT_PTR kTimerSubCloseId = 11;
inline constexpr UINT_PTR kTimerFlyoutHideId = 12;
inline constexpr UINT_PTR kTimerMenuHideId = 13;
inline constexpr UINT_PTR kTimerSubmenuHideId = 14;

inline constexpr UINT kTrayUid = 1;
inline constexpr UINT kMsgTray = WM_APP + 10;
inline constexpr UINT kMsgReposition = WM_APP + 11;
inline constexpr UINT kMsgSensorData = WM_APP + 12;
inline constexpr UINT kMsgAnimTick = WM_APP + 13;

inline constexpr UINT kSubStyle = 9001;
inline constexpr UINT kSubTheme = 9002;
inline constexpr UINT kSubRow2 = 9003;
inline constexpr UINT kSubRate = 9004;
inline constexpr UINT kSubDisplay = 9005;

class VsyncPacer {
public:
    ~VsyncPacer() { Stop(); }
    void Start(HWND hwndTarget, UINT msgTick);
    void Stop();
    bool Wake();
    void SleepUntilWoken();
    void OnTickReceived();

private:
    void Run();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    HWND m_hwnd = nullptr;
    UINT m_msg = 0;
    bool m_stop = false;
    bool m_active = false;
    std::atomic<bool> m_tickPending{ false };
};

class SensorWorker {
public:
    ~SensorWorker() { Stop(); }
    void Start(HWND hwndTarget, UINT msgData, int intervalMs);
    void Stop();
    void SetInterval(int intervalMs);
    bool FetchLatest(SystemSnapshot& outSnap, HistoryBuffer& outHistory, uint64_t& outSeq);

private:
    void Run();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    HWND m_hwnd = nullptr;
    UINT m_msg = 0;
    int m_intervalMs = 1000;
    bool m_stop = false;
    bool m_intervalChanged = false;

    SystemSnapshot m_latestSnap{};
    HistoryBuffer m_latestHistory{};
    uint64_t m_seq = 0;
};

struct TrayAsyncState {
    PTP_WORK work = nullptr;
    SRWLOCK lock = SRWLOCK_INIT;
    NOTIFYICONDATAW pendingNid{};
    bool hasPending = false;
    bool shutdown = false;
    std::wstring lastTip;
};

struct WidgetInstance {
    HWND hwnd = nullptr;
    HWND hwndTaskbar = nullptr;
    bool isPrimary = false;
    std::wstring deviceName;

    TaskbarPlacement lastPlacement{};
    bool visible = false;
    bool fullscreenPaused = false;
    bool hovered = false;
    bool tracking = false;
    bool pressed = false;

    float hoverProgress = 0.0f;
    float pressProgress = 0.0f;
    bool hasAnimX = false;
    float animX = 0.0f;

    bool draggingOffset = false;
    POINT dragStartScreen{};
    int dragStartOffsetDip = 0;
    TaskbarInfo dragTaskbar{};
    std::vector<HWND> dragOwnWindows;
    WidgetDimensions dragDim{};
};

struct AppState {
    HINSTANCE hInst = nullptr;
    HICON hIcon = nullptr;
    HWND hwndHost = nullptr;
    HWND hwndFlyout = nullptr;
    HWND hwndMenu = nullptr;
    HWND hwndSubmenu = nullptr;
    UINT msgTaskbarCreated = 0;

    AppConfig cfg{};
    SensorWorker sensorWorker;
    SystemSnapshot snap{};
    HistoryBuffer history{};
    uint64_t lastSensorSeq = 0;
    GoldPrice goldPrice;
    RenderEngine renderer;
    TaskbarHost host;
    VsyncPacer pacer;
    LARGE_INTEGER qpcFreq{};
    LARGE_INTEGER lastAnimQpc{};
    AnimatedMetrics anim{};

    std::vector<WidgetInstance> widgets;
    HWND flyoutAnchorWidget = nullptr;

    bool flyoutVisible = false;
    bool flyoutTaskbarAtTop = false;
    UINT flyoutDpi = 96;
    bool suppressFlyoutDeactivate = false;
    ULONGLONG lastFlyoutClosedTick = 0;
    bool flyoutSettingsPage = false;
    bool flyoutSheetOpen = false;
    int flyoutSheetHoverIdx = -1;
    float flyoutSheetScrollTarget = 0.0f;
    std::vector<FlyoutDisplayItem> flyoutDisplayItems;
    bool autoStartCached = false;
    int flyoutHoverRow = -1;
    FlyoutControl flyoutHoverCtrl = FlyoutControl::None;
    FlyoutControl flyoutPressedCtrl = FlyoutControl::None;
    bool flyoutTracking = false;
    FlyoutHitMap flyoutHitMap{};
    FlyoutAnimState flyoutAnim{};

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
    std::vector<std::wstring> menuDisplayDevices;
    int menuHoverIdx = -1;
    int submenuHoverIdx = -1;
    int openSubIdx = -1;
    int pendingSubIdx = -1;
    MenuAnimState menuAnim{};
    MenuAnimState submenuAnim{};

    bool trayAdded = false;
    TrayAsyncState trayAsync{};
    bool repositionPending = false;
};

inline AppState* GetApp(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void RequestAnimationFrame(AppState* app);
void KickInteractiveAnimation(AppState* app, HWND focusHwnd = nullptr);
void SyncAnimatedMetricTargetsImmediate(AppState* app);
void ApplyRefreshTimer(AppState* app);
void ApplyGoldTimer(AppState* app);
void ExecuteMenuCommand(AppState* app, UINT cmd);
int RunApplication(HINSTANCE hInstance);
