#include "../src/taskbar/taskbar_layout.hpp"
#include "../src/taskbar/taskbar_host.hpp"
#include <cstdio>

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT bounds{};
    if (!taskbar || !GetWindowRect(taskbar, &bounds)) return 2;
    UINT dpi = GetDpiForWindow(taskbar);
    TaskbarLayoutReader reader;
    std::vector<RECT> occupied;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (reader.Read(taskbar, bounds, dpi, occupied)) {
            std::printf("Taskbar=(%ld,%ld)-(%ld,%ld), DPI=%u, controls=%zu\n",
                bounds.left, bounds.top, bounds.right, bounds.bottom, dpi, occupied.size());
            for (const auto& rect : occupied) {
                std::printf("Occupied=(%ld,%ld)-(%ld,%ld)\n",
                    rect.left, rect.top, rect.right, rect.bottom);
            }
            TaskbarHost host;
            for (int offset : {0, 242, -800}) {
                TaskbarPlacement placement;
                for (int retry = 0; retry < 50; ++retry) {
                    placement = host.ComputePlacement(434, 40, offset, nullptr, nullptr);
                    if (placement.shouldShow) break;
                    Sleep(100);
                }
                if (!placement.shouldShow) return 4;
                RECT widget{placement.ptWidget.x, placement.ptWidget.y,
                    placement.ptWidget.x + placement.szWidget.cx,
                    placement.ptWidget.y + placement.szWidget.cy};
                for (const auto& rect : occupied) {
                    RECT intersection{};
                    if (IntersectRect(&intersection, &widget, &rect)) return 5;
                }
                POINT screen = placement.ptWidgetClient;
                MapWindowPoints(taskbar, nullptr, &screen, 1);
                if (screen.x != placement.ptWidget.x || screen.y != placement.ptWidget.y) return 6;
                std::printf("Offset=%d, widget=(%ld,%ld)-(%ld,%ld), appliedOffset=%d\n",
                    offset, widget.left, widget.top, widget.right, widget.bottom,
                    placement.appliedOffsetXDip);
            }
            RECT changed = bounds;
            changed.right -= 100;
            if (reader.Read(taskbar, changed, dpi, occupied)) return 3;
            std::puts("PASS: live taskbar bounds read; stale display layout rejected");
            return 0;
        }
        Sleep(100);
    }
    std::puts("FAIL: no usable taskbar layout within 5 seconds");
    return 1;
}
