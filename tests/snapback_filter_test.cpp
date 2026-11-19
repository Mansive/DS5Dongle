// Host test for src/snapback_filter.cpp. Built by tests/test_snapback_filter.py.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "snapback_filter.h"
#include "utils.h"

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static uint32_t ms(const double t) { return static_cast<uint32_t>(t * SNAPBACK_TICKS_PER_MS); }

// One stick sample: time in ms from some base, x and y as -1..1 of full travel.
struct Sample {
    double t;
    double x;
    double y;
};

static uint8_t axis(const double v) {
    const int raw = 0x80 + static_cast<int>(v * 127.0 + (v < 0 ? -0.5 : 0.5));
    return static_cast<uint8_t>(raw < 0 ? 0 : raw > 255 ? 255 : raw);
}

// A right-stick flick let go at t = 0, shaped like the measured aftermarket
// stick: back at centre in about 5 ms, 55% past it, 30% rebound, settled by 30 ms.
static const Sample release[] = {
    {-20, 1.0, 0}, {-10, 1.0, 0}, {0, 0.84, 0}, {2, 0.5, 0}, {4, 0.12, 0}, {6, -0.3, 0},
    {8, -0.55, 0}, {10, -0.4, 0}, {12, 0.0, 0}, {14, 0.25, 0}, {16, 0.3, 0}, {18, 0.15, 0},
    {20, -0.05, 0}, {22, -0.12, 0}, {26, -0.04, 0}, {30, 0.02, 0}, {40, 0.0, 0},
};

static bool step(SnapbackStick &s, const Sample &p, const uint8_t hold, const uint32_t base = 0) {
    return snapback_step(s, axis(p.x), axis(p.y), base + ms(p.t + 100), hold);
}

static void test_off_passes_through() {
    SnapbackStick s;
    for (const auto &p : release) CHECK(!step(s, p, 0));
}

static void test_release_is_held() {
    SnapbackStick s;
    for (const auto &p : release) {
        const bool held = step(s, p, 30);
        // Enters the 15% zone at 4 ms; the hold lasts 30 ms from there.
        const bool expect = p.t >= 4 && p.t < 34;
        if (held != expect) std::printf("  t=%.0f held=%d\n", p.t, held);
        CHECK(held == expect);
    }
}

static void test_short_hold_ends_early() {
    SnapbackStick s;
    for (const auto &p : release) {
        const bool held = step(s, p, 10);
        CHECK(held == (p.t >= 4 && p.t < 14));
    }
}

static void test_slow_return_is_not_held() {
    // The player brings the stick back by hand over 80 ms.
    SnapbackStick s;
    for (int t = 0; t <= 80; t += 4) {
        const double x = 1.0 - t / 80.0;
        CHECK(!step(s, {static_cast<double>(t), x, 0}, 30));
    }
}

static void test_small_flick_is_not_held() {
    // Never reaches 50%: no release to filter.
    SnapbackStick s;
    const Sample small[] = {{0, 0.45, 0}, {2, 0.3, 0}, {4, 0.05, 0}, {6, -0.2, 0}, {8, 0.0, 0}};
    for (const auto &p : small) CHECK(!step(s, p, 30));
}

static void test_hard_push_escapes() {
    // Let go, then flick hard the other way 8 ms later: that goes through.
    SnapbackStick s;
    const Sample flick[] = {{0, 1.0, 0}, {2, 0.4, 0}, {4, 0.1, 0}, {6, -0.5, 0}, {8, -1.0, 0}, {10, -1.0, 0}};
    const bool expect[] = {false, false, true, true, false, false};
    for (int i = 0; i < 6; i++) CHECK(step(s, flick[i], 30) == expect[i]);
    // Letting go of that flick is filtered again.
    CHECK(!step(s, {12, 0.6, 0}, 30));
    CHECK(step(s, {14, -0.05, 0}, 30));
}

static void test_jump_past_centre() {
    // With a report every 4 ms the stick can go from 60% straight to 40% past
    // centre without a report near centre.
    SnapbackStick s;
    const Sample jump[] = {{0, 1.0, 0}, {4, 0.6, 0}, {8, -0.4, 0.1}, {12, 0.2, 0}, {16, 0.0, 0}};
    const bool expect[] = {false, false, true, true, true};
    for (int i = 0; i < 5; i++) CHECK(step(s, jump[i], 30) == expect[i]);
}

static void test_sideways_move_is_not_held() {
    // Swinging the stick from right to up keeps it far from centre along the
    // first direction only briefly; it never comes back to centre, so no hold.
    SnapbackStick s;
    const Sample swing[] = {{0, 1.0, 0}, {4, 0.7, 0.7}, {8, 0.0, 1.0}, {12, 0.0, 1.0}};
    for (const auto &p : swing) CHECK(!step(s, p, 30));
}

static void test_diagonal() {
    SnapbackStick s;
    const Sample diag[] = {{0, 0.7, 0.7}, {2, 0.3, 0.3}, {4, 0.05, 0.05}, {6, -0.3, -0.3}};
    const bool expect[] = {false, false, true, true};
    for (int i = 0; i < 4; i++) CHECK(step(s, diag[i], 30) == expect[i]);
}

static void test_timestamp_wrap() {
    SnapbackStick s;
    const uint32_t base = 0xFFFFFFFFu - ms(110); // wraps around t = 10 ms
    for (const auto &p : release) {
        const bool held = step(s, p, 30, base);
        CHECK(held == (p.t >= 4 && p.t < 34));
    }
}

static void test_repeated_releases() {
    SnapbackStick s;
    for (int n = 0; n < 3; n++) {
        const uint32_t base = ms(200 * n);
        for (const auto &p : release) CHECK(step(s, p, 30, base) == (p.t >= 4 && p.t < 34));
    }
}

static void test_apply_centres_only_that_stick() {
    SnapbackFilter f;
    USBGetStateData st;
    std::memset(&st, 0, sizeof(st));
    auto feed = [&](const double t, const double rx, const uint8_t left_ms, const uint8_t right_ms) {
        st.LeftStickX = axis(-0.6);
        st.LeftStickY = axis(0.2);
        st.RightStickX = axis(rx);
        st.RightStickY = axis(0);
        st.SensorTimestamp = ms(t);
        snapback_filter_apply(f, st, left_ms, right_ms);
    };
    feed(0, 1.0, 30, 30);
    feed(2, 0.1, 30, 30);
    CHECK(st.RightStickX == 0x80 && st.RightStickY == 0x80);
    CHECK(st.LeftStickX == axis(-0.6) && st.LeftStickY == axis(0.2));
    feed(4, -0.5, 30, 30);
    CHECK(st.RightStickX == 0x80);
    // Right stick off: passes through even mid-release.
    SnapbackFilter g;
    f = g;
    feed(0, 1.0, 30, 0);
    feed(2, 0.1, 30, 0);
    CHECK(st.RightStickX == axis(0.1));
}

int main() {
    test_off_passes_through();
    test_release_is_held();
    test_short_hold_ends_early();
    test_slow_return_is_not_held();
    test_small_flick_is_not_held();
    test_hard_push_escapes();
    test_jump_past_centre();
    test_sideways_move_is_not_held();
    test_diagonal();
    test_timestamp_wrap();
    test_repeated_releases();
    test_apply_centres_only_that_stick();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("snapback filter: all tests passed\n");
    return 0;
}
