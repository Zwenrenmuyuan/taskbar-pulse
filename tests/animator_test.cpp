#include "../src/render/animator.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    // 1. Exponential damping converges to target and settles cleanly
    float v60 = 0.0f;
    bool moving = true;
    int steps60 = 0;
    while (moving && steps60 < 500) {
        moving = anim::Damp(v60, 1.0f, 18.0f, 1.0f / 60.0f);
        ++steps60;
    }
    assert(!moving);
    assert(v60 == 1.0f);
    assert(steps60 < 60); // settles well within 1 second

    // 2. Frame-rate independence: 60Hz vs 120Hz after same elapsed time (0.1s)
    float a60 = 0.0f;
    for (int i = 0; i < 6; ++i) anim::Damp(a60, 100.0f, 12.0f, 1.0f / 60.0f, 0.0001f);
    float a120 = 0.0f;
    for (int i = 0; i < 12; ++i) anim::Damp(a120, 100.0f, 12.0f, 1.0f / 120.0f, 0.0001f);
    assert(std::fabs(a60 - a120) < 0.01f);

    // 3. Double damping settles cleanly
    double pct = 10.0;
    for (int i = 0; i < 120; ++i) anim::DampDouble(pct, 85.0, 10.5f, 1.0f / 60.0f, 0.05);
    assert(pct == 85.0);

    // 4. Color lerp clamping and midpoint
    D2D1_COLOR_F c0 = D2D1::ColorF(0.0f, 0.5f, 1.0f, 0.2f);
    D2D1_COLOR_F c1 = D2D1::ColorF(1.0f, 0.5f, 0.0f, 1.0f);
    D2D1_COLOR_F mid = anim::LerpColor(c0, c1, 0.5f);
    assert(std::fabs(mid.r - 0.5f) < 1e-5f);
    assert(std::fabs(mid.g - 0.5f) < 1e-5f);
    assert(std::fabs(mid.b - 0.5f) < 1e-5f);
    assert(std::fabs(mid.a - 0.6f) < 1e-5f);

    D2D1_COLOR_F cur = c0;
    bool colMoving = true;
    int colSteps = 0;
    while (colMoving && colSteps < 300) {
        colMoving = anim::DampColor(cur, c1, 14.0f, 1.0f / 60.0f);
        ++colSteps;
    }
    assert(!colMoving);
    assert(cur.r == c1.r && cur.g == c1.g && cur.b == c1.b && cur.a == c1.a);

    std::puts("PASS: animator damping, frame-rate independence, and color interpolation");
    return 0;
}
