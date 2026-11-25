#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "taskbar/taskbar_host.hpp"
#include "taskbar/placement.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <map>

namespace {

TaskbarHost* g_hostInstance = nullptr;

HWND FindChildByClassRecursive(HWND parent, const wchar_t* clsName) {
    HWND direct = FindWindowExW(parent, nullptr, clsName, nullptr);
    if (direct) return direct;

    struct SearchCtx {
        const wchar_t* cls;
        HWND found;
    } ctx{ clsName, nullptr };

    EnumChildWindows(parent, [](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<SearchCtx*>(lp);
        wchar_t buf[64]{};
        if (GetClassNameW(hwnd, buf, _countof(buf)) > 0 && wcscmp(buf, c->cls) == 0) {
            c->found = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

std::map<std::wstring, std::wstring> QueryMonitorFriendlyNames() {
    std::map<std::wstring, std::wstring> names;
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS ||
            pathCount == 0) {
            return names;
        }
        paths.resize(pathCount);
        modes.resize(modeCount);
        LONG rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
            &modeCount, modes.data(), nullptr);
        if (rc == ERROR_SUCCESS) {
            paths.resize(pathCount);
            break;
        }
        if (rc != ERROR_INSUFFICIENT_BUFFER) return names;
    }

    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME srcName{};
        srcName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        srcName.header.size = sizeof(srcName);
        srcName.header.adapterId = path.sourceInfo.adapterId;
        srcName.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&srcName.header) != ERROR_SUCCESS ||
            srcName.viewGdiDeviceName[0] == L'\0') {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME dstName{};
        dstName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        dstName.header.size = sizeof(dstName);
        dstName.header.adapterId = path.targetInfo.adapterId;
        dstName.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&dstName.header) == ERROR_SUCCESS &&
            dstName.monitorFriendlyDeviceName[0] != L'\0') {
            names[srcName.viewGdiDeviceName] = dstName.monitorFriendlyDeviceName;
        }
    }
    return names;
}

bool IsFullscreenCoveringTaskbar(HWND hTaskbar, const std::vector<HWND>& ownWindows,
                                 HMONITOR hTbMon, const RECT& rcMon) {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == hTaskbar) return false;
    if (std::find(ownWindows.begin(), ownWindows.end(), fg) != ownWindows.end()) return false;
    if (!IsWindowVisible(fg) || IsIconic(fg)) return false;

    wchar_t cls[64]{};
    if (GetClassNameW(fg, cls, _countof(cls)) > 0) {
        if (wcscmp(cls, L"Progman") == 0 ||
            wcscmp(cls, L"WorkerW") == 0 ||
            wcscmp(cls, L"Shell_TrayWnd") == 0 ||
            wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
            return false;
        }
    }

    QUERY_USER_NOTIFICATION_STATE state{};
    if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
        state == QUNS_RUNNING_D3D_FULL_SCREEN) {
        HMONITOR hFgMon = MonitorFromWindow(fg, MONITOR_DEFAULTTONULL);
        if (!hFgMon || hFgMon == hTbMon) {
            return true;
        }
    }

    RECT rcFg{};
    if (!GetWindowRect(fg, &rcFg)) return false;

    if (rcFg.left > rcMon.left ||
        rcFg.top > rcMon.top ||
        rcFg.right < rcMon.right ||
        rcFg.bottom < rcMon.bottom) {
        return false;
    }

    // Regular maximized windows (with title bar) can extend past monitor edges
    // via invisible DWM borders when the taskbar is auto-hidden; do not treat
    // them as fullscreen apps.
    if (IsZoomed(fg) &&
        (GetWindowLongPtrW(fg, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {
        return false;
    }

    return true;
}

bool IsWindowBelowInZOrder(HWND target, HWND reference) {
    if (!target || !reference || target == reference) return false;
    if (GetWindow(target, GW_HWNDNEXT) == reference) return false;
    HWND cur = GetWindow(target, GW_HWNDPREV);
    for (int steps = 0; cur && steps < 48; ++steps) {
        if (cur == reference) return true;
        cur = GetWindow(cur, GW_HWNDPREV);
    }
    return false;
}

bool HasVisibleSiblingAbove(HWND child) {
    for (HWND cur = GetWindow(child, GW_HWNDPREV); cur; cur = GetWindow(cur, GW_HWNDPREV)) {
        if (IsWindowVisible(cur)) return true;
    }
    return false;
}

} // namespace

TaskbarHost::~TaskbarHost() {
    DetachHook();
}

void TaskbarHost::RefreshHandles(bool refreshNames) {
    HWND primaryTb = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (primaryTb != m_hTaskbar || (m_hTrayNotify && !IsWindow(m_hTrayNotify))) {
        m_hTaskbar = primaryTb;
        m_hTrayNotify = m_hTaskbar ? FindChildByClassRecursive(m_hTaskbar, L"TrayNotifyWnd") : nullptr;
        DWORD pid = 0;
        if (m_hTaskbar) GetWindowThreadProcessId(m_hTaskbar, &pid);
        if (pid != m_explorerPid) {
            m_explorerPid = pid;
            if (m_hook) {
                UnhookWinEvent(m_hook);
                m_hook = nullptr;
            }
            if (m_explorerPid != 0 && m_cb) {
                m_hook = SetWinEventHook(
                    EVENT_OBJECT_REORDER, EVENT_OBJECT_LOCATIONCHANGE,
                    nullptr, &TaskbarHost::WinEventProc,
                    m_explorerPid, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            }
        }
    } else if (m_hTaskbar && !m_hTrayNotify) {
        m_hTrayNotify = FindChildByClassRecursive(m_hTaskbar, L"TrayNotifyWnd");
    }

    struct MonEntry {
        HMONITOR hMon = nullptr;
        bool isPrimary = false;
        std::wstring deviceName;
        RECT rcMonitor{};
    };
    std::vector<MonEntry> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* list = reinterpret_cast<std::vector<MonEntry>*>(lp);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi)) {
            MonEntry e;
            e.hMon = hMon;
            e.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            e.deviceName = mi.szDevice;
            e.rcMonitor = mi.rcMonitor;
            list->push_back(std::move(e));
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));

    std::sort(monitors.begin(), monitors.end(), [](const MonEntry& a, const MonEntry& b) {
        if (a.isPrimary != b.isPrimary) return a.isPrimary > b.isPrimary;
        if (a.rcMonitor.left != b.rcMonitor.left) return a.rcMonitor.left < b.rcMonitor.left;
        return a.rcMonitor.top < b.rcMonitor.top;
    });

    std::vector<std::wstring> currentDevices;
    currentDevices.reserve(monitors.size());
    for (const auto& m : monitors) currentDevices.push_back(m.deviceName);
    if (refreshNames || currentDevices != m_cachedMonitorDevices) {
        m_friendlyNames = QueryMonitorFriendlyNames();
        m_cachedMonitorDevices = std::move(currentDevices);
    }
    const auto& friendlyNames = m_friendlyNames;

    std::vector<HWND> rawTaskbars;
    if (m_hTaskbar && IsWindow(m_hTaskbar)) {
        rawTaskbars.push_back(m_hTaskbar);
    }
    HWND sec = nullptr;
    while ((sec = FindWindowExW(nullptr, sec, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr) {
        if (IsWindow(sec) && sec != m_hTaskbar) {
            rawTaskbars.push_back(sec);
        }
    }

    auto makeInfo = [&](HWND hwndTb, bool isPrimaryHint) -> TaskbarInfo {
        TaskbarInfo info;
        info.hwndTaskbar = hwndTb;
        info.isPrimary = isPrimaryHint;
        info.hwndTrayNotify = isPrimaryHint ? m_hTrayNotify : nullptr;
        info.hMonitor = MonitorFromWindow(hwndTb, MONITOR_DEFAULTTONEAREST);
        UINT dpi = GetDpiForWindow(hwndTb);
        info.dpi = dpi ? dpi : 96;

        auto mit = std::find_if(monitors.begin(), monitors.end(), [&](const MonEntry& m) {
            return m.hMon == info.hMonitor;
        });
        if (mit != monitors.end()) {
            info.deviceName = mit->deviceName;
            info.rcMonitor = mit->rcMonitor;
        } else if (info.hMonitor) {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(info.hMonitor, &mi)) {
                info.deviceName = mi.szDevice;
                info.rcMonitor = mi.rcMonitor;
            }
        }
        auto nit = friendlyNames.find(info.deviceName);
        if (nit != friendlyNames.end()) {
            info.friendlyName = nit->second;
        }
        return info;
    };

    std::vector<TaskbarInfo> collected;
    collected.reserve(rawTaskbars.size());
    for (HWND hwndTb : rawTaskbars) {
        collected.push_back(makeInfo(hwndTb, hwndTb == m_hTaskbar));
    }

    std::sort(collected.begin(), collected.end(), [](const TaskbarInfo& a, const TaskbarInfo& b) {
        if (a.isPrimary != b.isPrimary) return a.isPrimary > b.isPrimary;
        if (a.rcMonitor.left != b.rcMonitor.left) return a.rcMonitor.left < b.rcMonitor.left;
        return a.rcMonitor.top < b.rcMonitor.top;
    });

    m_taskbars = std::move(collected);

    std::vector<HWND> activeHwnds;
    activeHwnds.reserve(m_taskbars.size());
    for (const auto& tb : m_taskbars) activeHwnds.push_back(tb.hwndTaskbar);
    m_layout.Prune(activeHwnds);
}

std::vector<TaskbarInfo> TaskbarHost::ResolveTargetTaskbars(const AppConfig& cfg) const {
    std::vector<TaskbarInfo> result;
    if (m_taskbars.empty()) return result;

    if (cfg.displayMode == DisplayTargetMode::All) {
        for (const auto& tb : m_taskbars) {
            if (tb.hwndTaskbar && IsWindow(tb.hwndTaskbar) &&
                (tb.isPrimary || IsWindowVisible(tb.hwndTaskbar))) {
                result.push_back(tb);
            }
        }
        return result;
    }

    if (cfg.displayMode == DisplayTargetMode::Specific && !cfg.displayDevice.empty()) {
        const std::wstring wantKey = SanitizeMonitorKey(cfg.displayDevice);
        for (const auto& tb : m_taskbars) {
            if (!tb.hwndTaskbar || !IsWindow(tb.hwndTaskbar)) continue;
            if (!tb.isPrimary && !IsWindowVisible(tb.hwndTaskbar)) continue;
            if (SanitizeMonitorKey(tb.deviceName) == wantKey) {
                result.push_back(tb);
                return result;
            }
        }
        // Fallback to primary taskbar when the preferred monitor is unavailable.
    }

    for (const auto& tb : m_taskbars) {
        if (tb.isPrimary && tb.hwndTaskbar && IsWindow(tb.hwndTaskbar)) {
            result.push_back(tb);
            return result;
        }
    }
    if (m_taskbars.front().hwndTaskbar && IsWindow(m_taskbars.front().hwndTaskbar)) {
        result.push_back(m_taskbars.front());
    }
    return result;
}

bool TaskbarHost::IsTrackedTaskbarHwnd(HWND hwnd) const {
    if (!hwnd) return false;
    if (hwnd == m_hTaskbar || hwnd == m_hTrayNotify) return true;
    for (const auto& tb : m_taskbars) {
        if (hwnd == tb.hwndTaskbar || (tb.hwndTrayNotify && hwnd == tb.hwndTrayNotify)) {
            return true;
        }
    }
    return false;
}

void TaskbarHost::AttachHook(ChangeCallback cb, void* userData) {
    m_cb = cb;
    m_userData = userData;
    g_hostInstance = this;
    RefreshHandles();
    if (!m_fgHook && m_cb) {
        m_fgHook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            nullptr, &TaskbarHost::WinEventProc,
            0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
}

void TaskbarHost::DetachHook() {
    if (m_hook) {
        UnhookWinEvent(m_hook);
        m_hook = nullptr;
    }
    if (m_fgHook) {
        UnhookWinEvent(m_fgHook);
        m_fgHook = nullptr;
    }
    if (g_hostInstance == this) {
        g_hostInstance = nullptr;
    }
    m_cb = nullptr;
    m_userData = nullptr;
}

void CALLBACK TaskbarHost::WinEventProc(
    HWINEVENTHOOK,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG,
    DWORD,
    DWORD) {
    if (!g_hostInstance || !g_hostInstance->m_cb) return;

    if (event == EVENT_SYSTEM_FOREGROUND) {
        g_hostInstance->m_cb(g_hostInstance->m_userData);
        return;
    }

    if ((event == EVENT_OBJECT_LOCATIONCHANGE || event == EVENT_OBJECT_REORDER) &&
        idObject == OBJID_WINDOW &&
        g_hostInstance->IsTrackedTaskbarHwnd(hwnd)) {
        g_hostInstance->m_cb(g_hostInstance->m_userData);
    }
}

TaskbarPlacement TaskbarHost::ComputePlacementForTaskbar(
    const TaskbarInfo& tb,
    float widgetWidthDip,
    float widgetMaxHeightDip,
    int offsetXDip,
    const std::vector<HWND>& ownWindows) {
    TaskbarPlacement p{};
    HWND hTaskbar = tb.hwndTaskbar;
    if (!hTaskbar || !IsWindow(hTaskbar)) return p;

    RECT rcTb{};
    if (!GetWindowRect(hTaskbar, &rcTb)) return p;

    HMONITOR hMon = MonitorFromWindow(hTaskbar, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return p;

    UINT dpi = GetDpiForWindow(hTaskbar);
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;

    p.valid = true;
    p.dpi = dpi;
    p.rcTaskbar = rcTb;
    p.rcMonitor = mi.rcMonitor;

    int tbW = rcTb.right - rcTb.left;
    int tbH = rcTb.bottom - rcTb.top;
    if (tbW <= 0 || tbH <= 0 || !IsWindowVisible(hTaskbar)) {
        p.shouldShow = false;
        return p;
    }

    // Check if taskbar is auto-hidden offscreen
    bool offBottom = (rcTb.top >= mi.rcMonitor.bottom - 2);
    bool offTop = (rcTb.bottom <= mi.rcMonitor.top + 2);
    if (offBottom || offTop) {
        p.shouldShow = false;
        return p;
    }

    p.taskbarAtTop = (rcTb.top - mi.rcMonitor.top) < (mi.rcMonitor.bottom - rcTb.bottom);

    if (IsFullscreenCoveringTaskbar(hTaskbar, ownWindows, hMon, mi.rcMonitor)) {
        p.occludedByFullscreen = true;
        return p;
    }

    // Compute pixel size respecting taskbar height
    float maxHeightDip = std::max(28.0f, (static_cast<float>(tbH) / scale) - 6.0f);
    float hDip = std::min(widgetMaxHeightDip, maxHeightDip);
    int wPx = std::max(1, static_cast<int>(std::lround(widgetWidthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(hDip * scale)));

    int gapPx = static_cast<int>(std::lround(6.0f * scale));
    int edgePx = static_cast<int>(std::lround(4.0f * scale));
    int offsetPx = static_cast<int>(std::lround(static_cast<float>(offsetXDip) * scale));

    RECT visibleTaskbar{};
    std::vector<RECT> occupiedRects;
    if (!IntersectRect(&visibleTaskbar, &rcTb, &mi.rcMonitor) ||
        !m_layout.Read(hTaskbar, rcTb, dpi, occupiedRects)) return p;

    int rightBound = static_cast<int>(visibleTaskbar.right) - edgePx;
    int defaultX = rightBound - wPx;
    if (tb.isPrimary) {
        int trayLeft = rcTb.right - static_cast<int>(std::lround(150.0f * scale));
        if (tb.hwndTrayNotify && IsWindow(tb.hwndTrayNotify)) {
            RECT rcTray{};
            if (GetWindowRect(tb.hwndTrayNotify, &rcTray) && (rcTray.right - rcTray.left) > 10 &&
                rcTray.left >= rcTb.left && rcTray.right <= rcTb.right &&
                rcTray.top < rcTb.bottom && rcTray.bottom > rcTb.top) {
                trayLeft = rcTray.left;
            }
        }
        rightBound = std::min(rightBound, trayLeft - 2);
        defaultX = trayLeft - gapPx - wPx;
    }

    std::vector<HorizontalSpan> occupied;
    occupied.reserve(occupiedRects.size());
    for (const auto& rect : occupiedRects) occupied.push_back({ rect.left, rect.right });

    auto freeX = FindFreeTaskbarPosition(visibleTaskbar.left + edgePx,
        rightBound, wPx, defaultX + offsetPx, gapPx, std::move(occupied));
    // Keep the tray entry available when no safe interval fits. Do not overwrite
    // the user's preferred offset: it should recover when the larger screen returns.
    if (!freeX) return p;
    int x = *freeX;

    int y = rcTb.top + (tbH - hPx) / 2;

    POINT ptClient = POINT{ x, y };
    MapWindowPoints(nullptr, hTaskbar, &ptClient, 1);

    p.shouldShow = true;
    p.appliedOffsetXDip = static_cast<int>(std::lround((x - defaultX) / scale));
    p.ptWidget = POINT{ x, y };
    p.ptWidgetClient = ptClient;
    p.szWidget = SIZE{ wPx, hPx };
    return p;
}

TaskbarPlacement TaskbarHost::ComputePlacement(
    float widgetWidthDip,
    float widgetMaxHeightDip,
    int offsetXDip,
    HWND ownWidget,
    HWND ownFlyout) {
    RefreshHandles();
    std::vector<HWND> ownWindows;
    if (ownWidget) ownWindows.push_back(ownWidget);
    if (ownFlyout) ownWindows.push_back(ownFlyout);

    for (const auto& tb : m_taskbars) {
        if (tb.isPrimary) {
            return ComputePlacementForTaskbar(tb, widgetWidthDip, widgetMaxHeightDip, offsetXDip, ownWindows);
        }
    }
    if (!m_taskbars.empty()) {
        return ComputePlacementForTaskbar(m_taskbars.front(), widgetWidthDip, widgetMaxHeightDip, offsetXDip, ownWindows);
    }
    return {};
}

void TaskbarHost::EnsureAboveTaskbar(HWND taskbarHwnd, HWND ownWidget, HWND ownFlyout) {
    if (!taskbarHwnd || !IsWindow(taskbarHwnd)) return;

    if (ownWidget && IsWindowVisible(ownWidget) && GetParent(ownWidget) == taskbarHwnd) {
        if (HasVisibleSiblingAbove(ownWidget)) {
            SetWindowPos(ownWidget, HWND_TOP, 0, 0, 0, 0,
                SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    if (ownFlyout && IsWindowVisible(ownFlyout) && IsWindowBelowInZOrder(ownFlyout, taskbarHwnd)) {
        SetWindowPos(ownFlyout, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    }
}

void TaskbarHost::EnsureAboveTaskbar(HWND ownWidget, HWND ownFlyout) {
    EnsureAboveTaskbar(m_hTaskbar, ownWidget, ownFlyout);
}
