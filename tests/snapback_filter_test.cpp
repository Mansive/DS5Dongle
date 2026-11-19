// Host test for src/snapback_filter.cpp. Built by tests/test_snapback_filter.py.
#include <cmath>
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

// Feeds samples with hold 30 and checks which are held.
static void check_sequence(const Sample *samples, const bool *expect, const int n, const uint8_t hold = 30) {
    SnapbackStick s;
    for (int i = 0; i < n; i++) {
        const bool held = step(s, samples[i], hold);
        if (held != expect[i]) std::printf("  t=%.0f held=%d\n", samples[i].t, held);
        CHECK(held == expect[i]);
    }
}

static void test_settle_ends_hold_early() {
    // The release above with a settling tail and a 60 ms hold: the hold ends
    // 8 ms after the stick settles within 3% (t = 26), not at 64 ms. The pass
    // through centre at 12 ms is too short to end it.
    SnapbackStick s;
    const Sample tail[] = {{26, -0.01, 0}, {28, 0.01, 0}, {30, 0.0, 0}, {32, -0.01, 0.01}, {34, 0.0, 0}, {36, 0.0, 0}};
    for (const auto &p : release) {
        if (p.t >= 26) break;
        CHECK(step(s, p, 60) == (p.t >= 4));
    }
    for (const auto &p : tail) {
        const bool held = step(s, p, 60);
        if (held != (p.t < 34)) std::printf("  t=%.0f held=%d\n", p.t, held);
        CHECK(held == (p.t < 34));
    }
}

static void test_sideways_push_escapes() {
    const Sample s[] = {{0, 1.0, 0}, {2, 0.4, 0}, {4, 0.1, 0}, {6, -0.3, 0}, {8, 0, 0.5}, {10, 0, 0.5}};
    const bool expect[] = {false, false, true, true, false, false};
    check_sequence(s, expect, 6);
}

static void test_bigger_swing_escapes() {
    // Swings back to 48% after a 30% overshoot: a bounce only gets smaller.
    const Sample s[] = {{0, 1.0, 0}, {2, 0.4, 0}, {4, 0.1, 0}, {6, -0.3, 0}, {8, 0.48, 0}, {10, 0.48, 0}};
    const bool expect[] = {false, false, true, true, false, false};
    check_sequence(s, expect, 6);
}

static void test_slow_push_escapes() {
    // Let go, then pushed the other way slowly: past 25% and still growing
    // 10 ms after the release, so it goes through.
    const Sample s[] = {{0, 1.0, 0},   {2, 0.4, 0},   {4, 0.1, 0},   {6, -0.15, 0}, {8, -0.2, 0},
                        {10, -0.25, 0}, {12, -0.3, 0}, {14, -0.35, 0}, {16, -0.4, 0}};
    const bool expect[] = {false, false, true, true, true, true, true, false, false};
    check_sequence(s, expect, 9);
}

// Up to `amount` counts of repeatable jitter on a raw axis.
static uint8_t jitter(const uint8_t raw, const int amount, uint32_t &seed) {
    if (amount == 0) return raw;
    seed = seed * 1664525u + 1013904223u;
    const int v = raw + static_cast<int>((seed >> 16) % (2 * amount + 1)) - amount;
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

// A spring release modelled as a damped swing: back at centre after
// swing_ms / 2 and overshooting by `overshoot` after swing_ms, with up to
// `noise` counts of jitter on both axes. Sampled every dt ms starting at
// `phase`, every report that would pass a 10% deadzone in the 60 ms after the
// release must be held, so none of the escape rules mistakes the bounce for
// the player.
static void check_modelled_release(const double swing_ms, const double overshoot, const double dt,
                                   const double phase, const int noise) {
    constexpr double PI = 3.14159265358979;
    const double tau = swing_ms / -std::log(overshoot);
    SnapbackStick s;
    uint32_t seed = static_cast<uint32_t>(phase * 1000 + dt * 100 + swing_ms);
    const uint32_t base = ms(100);
    snapback_step(s, axis(1.0), axis(0), base - ms(20), 60);
    snapback_step(s, axis(1.0), axis(0), base - ms(10), 60);
    bool entered = false;
    double entered_at = 0;
    for (double t = phase; t < 80; t += dt) {
        const double x = std::exp(-t / tau) * std::cos(PI * t / swing_ms);
        const uint8_t rx = jitter(axis(x), noise, seed);
        const uint8_t ry = jitter(axis(0), noise, seed);
        const bool held = snapback_step(s, rx, ry, base + ms(t), 60);
        if (held && !entered) {
            entered = true;
            entered_at = t;
        }
        if (entered && t < entered_at + 60 && std::fabs(x) >= 0.10 && !held) {
            std::printf("  swing=%.0f dt=%.0f phase=%.2f noise=%d t=%.2f x=%.2f passed\n", swing_ms, dt, phase,
                        noise, t, x);
            CHECK(held);
        }
    }
    CHECK(entered);
}

static void test_modelled_bounces_stay_held() {
    // Stock stick: 54% overshoot. JS13: 72% overshoot, slower swing. Each with
    // no jitter and with up to 2 counts of it.
    const double sticks[][2] = {{6, 0.54}, {8, 0.72}};
    const double rates[] = {1, 2, 4};
    for (const auto &stick : sticks) {
        for (const double dt : rates) {
            for (int noise = 0; noise <= 2; noise += 2) {
                for (double phase = 0; phase < dt; phase += 0.25) {
                    check_modelled_release(stick[0], stick[1], dt, phase, noise);
                }
            }
        }
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
    test_settle_ends_hold_early();
    test_sideways_push_escapes();
    test_bigger_swing_escapes();
    test_slow_push_escapes();
    test_modelled_bounces_stay_held();
    test_apply_centres_only_that_stick();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("snapback filter: all tests passed\n");
    return 0;
}
