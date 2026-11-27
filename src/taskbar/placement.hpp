#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

struct HorizontalSpan {
    int left;
    int right;
};

// Pick the nearest position that fits entirely in a free interval. On success,
// also return that interval if requested. All coordinates are screen pixels.
inline std::optional<int> FindFreeTaskbarPosition(
    int left, int right, int width, int preferredX, int gap,
    std::vector<HorizontalSpan> occupied, HorizontalSpan* freeSpan = nullptr) {
    if (width <= 0 || right - left < width) return std::nullopt;
    std::sort(occupied.begin(), occupied.end(), [](const auto& a, const auto& b) {
        return a.left < b.left;
    });

    std::optional<int> best;
    int64_t bestDistance = INT64_MAX;
    auto consider = [&](int begin, int end) {
        if (end - begin < width) return;
        int x = std::clamp(preferredX, begin, end - width);
        int64_t distance = static_cast<int64_t>(x) - preferredX;
        if (distance < 0) distance = -distance;
        if (distance < bestDistance) {
            best = x;
            bestDistance = distance;
            if (freeSpan) *freeSpan = {begin, end};
        }
    };

    int cursor = left;
    for (const auto& span : occupied) {
        if (span.right <= span.left) continue;
        int begin = std::max(left, span.left - gap);
        int end = std::min(right, span.right + gap);
        if (end <= cursor || begin >= right) continue;
        consider(cursor, begin);
        cursor = std::max(cursor, end);
    }
    consider(cursor, right);
    return best;
}
