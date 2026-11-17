#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "taskbar/taskbar_layout.hpp"

struct TaskbarPlacement {
    bool valid = false;
    bool shouldShow = false;
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
    void RefreshHandles();

    TaskbarPlacement ComputePlacement(float widgetWidthDip, float widgetMaxHeightDip, int offsetXDip, bool hideInFullscreen, HWND ownWidget, HWND ownFlyout);
    void EnsureAboveTaskbar(HWND ownWidget, HWND ownFlyout);

    HWND TaskbarHwnd() const { return m_hTaskbar; }

private:
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
    DWORD m_explorerPid = 0;
    HWINEVENTHOOK m_hook = nullptr;
    HWINEVENTHOOK m_fgHook = nullptr;
    ChangeCallback m_cb = nullptr;
    void* m_userData = nullptr;
    TaskbarLayoutReader m_layout;
};
