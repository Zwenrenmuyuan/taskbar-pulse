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

bool IsFullscreenAppActive(HWND hTaskbar, HWND ownWidget, HWND ownFlyout, const RECT& rcMon) {
    QUERY_USER_NOTIFICATION_STATE state{};
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        if (state == QUNS_BUSY ||
            state == QUNS_RUNNING_D3D_FULL_SCREEN ||
            state == QUNS_PRESENTATION_MODE) {
            return true;
        }
    }

    HWND fg = GetForegroundWindow();
    if (!fg || fg == hTaskbar || fg == ownWidget || fg == ownFlyout) return false;
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

    HMONITOR hFgMon = MonitorFromWindow(fg, MONITOR_DEFAULTTONULL);
    HMONITOR hTbMon = MonitorFromWindow(hTaskbar, MONITOR_DEFAULTTONEAREST);
    if (hFgMon != hTbMon) return false;

    RECT rcFg{};
    if (!GetWindowRect(fg, &rcFg)) return false;

    return rcFg.left <= rcMon.left &&
           rcFg.top <= rcMon.top &&
           rcFg.right >= rcMon.right &&
           rcFg.bottom >= rcMon.bottom;
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

void TaskbarHost::RefreshHandles() {
    HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (tb != m_hTaskbar || (m_hTrayNotify && !IsWindow(m_hTrayNotify))) {
        m_hTaskbar = tb;
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
        (hwnd == g_hostInstance->m_hTaskbar || hwnd == g_hostInstance->m_hTrayNotify)) {
        g_hostInstance->m_cb(g_hostInstance->m_userData);
    }
}

TaskbarPlacement TaskbarHost::ComputePlacement(
    float widgetWidthDip,
    float widgetMaxHeightDip,
    int offsetXDip,
    bool hideInFullscreen,
    HWND ownWidget,
    HWND ownFlyout) {
    TaskbarPlacement p{};
    RefreshHandles();
    if (!m_hTaskbar || !IsWindow(m_hTaskbar)) return p;

    RECT rcTb{};
    if (!GetWindowRect(m_hTaskbar, &rcTb)) return p;

    HMONITOR hMon = MonitorFromWindow(m_hTaskbar, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return p;

    UINT dpi = GetDpiForWindow(m_hTaskbar);
    if (dpi == 0) dpi = 96;
    float scale = static_cast<float>(dpi) / 96.0f;

    p.valid = true;
    p.dpi = dpi;
    p.rcTaskbar = rcTb;
    p.rcMonitor = mi.rcMonitor;

    int tbW = rcTb.right - rcTb.left;
    int tbH = rcTb.bottom - rcTb.top;
    if (tbW <= 0 || tbH <= 0 || !IsWindowVisible(m_hTaskbar)) {
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

    if (hideInFullscreen && IsFullscreenAppActive(m_hTaskbar, ownWidget, ownFlyout, mi.rcMonitor)) {
        p.shouldShow = false;
        return p;
    }

    // Compute pixel size respecting taskbar height
    float maxHeightDip = std::max(28.0f, (static_cast<float>(tbH) / scale) - 6.0f);
    float hDip = std::min(widgetMaxHeightDip, maxHeightDip);
    int wPx = std::max(1, static_cast<int>(std::lround(widgetWidthDip * scale)));
    int hPx = std::max(1, static_cast<int>(std::lround(hDip * scale)));

    int trayLeft = rcTb.right - static_cast<int>(std::lround(150.0f * scale));
    if (m_hTrayNotify && IsWindow(m_hTrayNotify)) {
        RECT rcTray{};
        if (GetWindowRect(m_hTrayNotify, &rcTray) && (rcTray.right - rcTray.left) > 10 &&
            rcTray.left >= rcTb.left && rcTray.right <= rcTb.right &&
            rcTray.top < rcTb.bottom && rcTray.bottom > rcTb.top) {
            trayLeft = rcTray.left;
        }
    }

    int gapPx = static_cast<int>(std::lround(6.0f * scale));
    int offsetPx = static_cast<int>(std::lround(static_cast<float>(offsetXDip) * scale));
    int defaultX = trayLeft - gapPx - wPx;
    RECT visibleTaskbar{};
    std::vector<RECT> occupiedRects;
    if (!IntersectRect(&visibleTaskbar, &rcTb, &mi.rcMonitor) ||
        !m_layout.Read(m_hTaskbar, rcTb, dpi, occupiedRects)) return p;
    std::vector<HorizontalSpan> occupied;
    for (const auto& rect : occupiedRects) occupied.push_back({ rect.left, rect.right });
    int edgePx = static_cast<int>(std::lround(4.0f * scale));
    auto freeX = FindFreeTaskbarPosition(visibleTaskbar.left + edgePx,
        std::min(static_cast<int>(visibleTaskbar.right) - edgePx, trayLeft - 2),
        wPx, defaultX + offsetPx, gapPx, std::move(occupied));
    // Keep the tray entry available when no safe interval fits. Do not overwrite
    // the user's preferred offset: it should recover when the larger screen returns.
    if (!freeX) return p;
    int x = *freeX;

    int y = rcTb.top + (tbH - hPx) / 2;

    POINT ptClient = POINT{ x, y };
    MapWindowPoints(nullptr, m_hTaskbar, &ptClient, 1);

    p.shouldShow = true;
    p.appliedOffsetXDip = static_cast<int>(std::lround((x - defaultX) / scale));
    p.ptWidget = POINT{ x, y };
    p.ptWidgetClient = ptClient;
    p.szWidget = SIZE{ wPx, hPx };
    return p;
}

void TaskbarHost::EnsureAboveTaskbar(HWND ownWidget, HWND ownFlyout) {
    if (!m_hTaskbar || !IsWindow(m_hTaskbar)) return;

    if (ownWidget && IsWindowVisible(ownWidget) && GetParent(ownWidget) == m_hTaskbar) {
        if (HasVisibleSiblingAbove(ownWidget)) {
            SetWindowPos(ownWidget, HWND_TOP, 0, 0, 0, 0,
                SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        }
    }
    if (ownFlyout && IsWindowVisible(ownFlyout) && IsWindowBelowInZOrder(ownFlyout, m_hTaskbar)) {
        SetWindowPos(ownFlyout, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    }
}
