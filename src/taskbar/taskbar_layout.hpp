#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// UI Automation runs on its own MTA thread: Explorer must never block painting
// or mouse input in the monitor. Only rectangles are transferred to the UI.
class TaskbarLayoutReader {
public:
    TaskbarLayoutReader();
    ~TaskbarLayoutReader();
    TaskbarLayoutReader(const TaskbarLayoutReader&) = delete;
    TaskbarLayoutReader& operator=(const TaskbarLayoutReader&) = delete;

    bool Read(HWND taskbar, const RECT& bounds, UINT dpi, std::vector<RECT>& occupied);
    void Prune(const std::vector<HWND>& activeTaskbars);

private:
    struct Layout {
        HWND taskbar = nullptr;
        RECT bounds{};
        UINT dpi = 0;
        bool valid = false;
        std::vector<RECT> occupied;
    };
    struct Entry {
        Layout request;
        Layout result;
        bool pending = false;
        ULONGLONG lastRequest = 0;
    };
    static bool SameDisplay(const Layout& a, const Layout& b);
    bool HasPendingLocked() const;
    void Run();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    size_t m_nextCursor = 0;
    std::vector<Entry> m_entries;
    std::thread m_thread;
};
