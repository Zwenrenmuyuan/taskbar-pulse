#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <map>
#include <string>
#include <vector>
#include "core/config.hpp"
#include "taskbar/taskbar_layout.hpp"

struct TaskbarInfo {
    HWND hwndTaskbar = nullptr;
    HWND hwndTrayNotify = nullptr;
    HMONITOR hMonitor = nullptr;
    bool isPrimary = false;
    std::wstring deviceName;
    std::wstring friendlyName;
    RECT rcMonitor{};
    UINT dpi = 96;
};

struct TaskbarPlacement {
    bool valid = false;
    bool shouldShow = false;
    bool occludedByFullscreen = false;
    bool taskbarAtTop = false;
    UINT dpi = 96;
    int appliedOffsetXDip = 0;
    POINT ptWidget{};
    POINT ptWidgetClient{};
    SIZE szWidget{};
    RECT rcTaskbar{};
    RECT rcMonitor{};
};

class TaskbarHost {
public:
    using ChangeCallback = void (*)(void* userData);

    TaskbarHost() = default;
    ~TaskbarHost();

    TaskbarHost(const TaskbarHost&) = delete;
    TaskbarHost& operator=(const TaskbarHost&) = delete;

    void AttachHook(ChangeCallback cb, void* userData);
    void DetachHook();
    void RefreshHandles(bool refreshNames = false);

    const std::vector<TaskbarInfo>& Taskbars() const { return m_taskbars; }
    std::vector<TaskbarInfo> ResolveTargetTaskbars(const AppConfig& cfg) const;

    TaskbarPlacement ComputePlacementForTaskbar(
        const TaskbarInfo& tb,
        float widgetWidthDip,
        float widgetMaxHeightDip,
        int offsetXDip,
        const std::vector<HWND>& ownWindows);

    TaskbarPlacement ComputePlacement(
        float widgetWidthDip,
        float widgetMaxHeightDip,
        int offsetXDip,
        HWND ownWidget,
        HWND ownFlyout);

    void EnsureAboveTaskbar(HWND taskbarHwnd, HWND ownWidget, HWND ownFlyout);
    void EnsureAboveTaskbar(HWND ownWidget, HWND ownFlyout);

    HWND TaskbarHwnd() const { return m_hTaskbar; }

private:
    bool IsTrackedTaskbarHwnd(HWND hwnd) const;

    static void CALLBACK WinEventProc(
        HWINEVENTHOOK hWinEventHook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD idEventThread,
        DWORD dwmsEventTime);

    HWND m_hTaskbar = nullptr;
    HWND m_hTrayNotify = nullptr;
    std::vector<TaskbarInfo> m_taskbars;
    std::map<std::wstring, std::wstring> m_friendlyNames;
    std::vector<std::wstring> m_cachedMonitorDevices;
    DWORD m_explorerPid = 0;
    HWINEVENTHOOK m_hook = nullptr;
    HWINEVENTHOOK m_fgHook = nullptr;
    ChangeCallback m_cb = nullptr;
    void* m_userData = nullptr;
    TaskbarLayoutReader m_layout;
};
