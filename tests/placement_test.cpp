#include "../src/taskbar/placement.hpp"
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <random>

int main() {
    // Company desktop: retain the preferred position to the left of the tray.
    assert(FindFreeTaskbarPosition(4, 2198, 434, 1760, 6,
        {{0, 160}, {900, 1600}, {2200, 2560}}) == 1760);
    // The same layout at 150% DPI preserves the logical placement.
    assert(FindFreeTaskbarPosition(6, 3297, 651, 2640, 9,
        {{0, 240}, {1350, 2400}, {3300, 3840}}) == 2640);
    // Smaller remote desktop: the right-hand gap is now too narrow; use the left.
    assert(FindFreeTaskbarPosition(4, 1568, 434, 1130, 6,
        {{0, 120}, {650, 1200}, {1570, 1920}}) == 210);
    // A user preference colliding with buttons moves to the nearest legal gap.
    assert(FindFreeTaskbarPosition(4, 2198, 434, 1500, 6,
        {{0, 160}, {900, 1600}}) == 1606);
    // With no space, leave only the tray entry instead of covering applications.
    assert(!FindFreeTaskbarPosition(4, 1100, 434, 660, 6,
        {{0, 120}, {400, 1000}, {1100, 1366}}));
    // Negative monitor origins, overlapping bounds, unsorted controls, exact fit.
    assert(FindFreeTaskbarPosition(-1916, -352, 434, -800, 6,
        {{-900, -500}, {-1920, -1800}, {-940, -600}}) == -1380);
    // Secondary taskbar on right monitor [2560, 4480] with no TrayNotifyWnd:
    // preferred right-edge alignment snaps flush beside the Win11 secondary clock.
    assert(FindFreeTaskbarPosition(2564, 4476, 356, 4476 - 356, 6,
        {{2560, 2608}, {3200, 3520}, {4390, 4476}}) == 4390 - 6 - 356);
    assert(FindFreeTaskbarPosition(0, 100, 100, 200, 0, {}) == 0);
    assert(!FindFreeTaskbarPosition(0, 99, 100, 0, 0, {}));

    // Compare against every legal pixel in random layouts, including fragmented
    // free space. This catches gaps/overlaps missed by hand-picked resolutions.
    std::mt19937 random(20261117);
    for (int sample = 0; sample < 10000; ++sample) {
        int left = -150;
        int right = 50 + static_cast<int>(random() % 400);
        int width = 1 + static_cast<int>(random() % 150);
        int gap = static_cast<int>(random() % 10);
        int preferred = static_cast<int>(random() % 1000) - 500;
        std::vector<HorizontalSpan> spans;
        for (unsigned n = random() % 10; n > 0; --n) {
            int begin = static_cast<int>(random() % 800) - 300;
            spans.push_back({begin, begin + 1 + static_cast<int>(random() % 100)});
        }
        auto result = FindFreeTaskbarPosition(left, right, width, preferred, gap, spans);
        int bestDistance = INT_MAX;
        for (int x = left; x + width <= right; ++x) {
            bool free = true;
            for (const auto& span : spans) {
                if (x < span.right + gap && x + width > span.left - gap) free = false;
            }
            if (free) bestDistance = std::min(bestDistance, std::abs(x - preferred));
        }
        assert(result.has_value() == (bestDistance != INT_MAX));
        if (result) assert(std::abs(*result - preferred) == bestDistance);
    }
    std::puts("PASS: resolution changes, icon avoidance, negative origins and 10000 layout cases");
}
