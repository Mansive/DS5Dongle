// Host test for src/snapback_filter.cpp. Built by tests/test_snapback_filter.py.
#include <cmath>
#include <cstddef>
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

// Feeds samples to a fresh stick and checks that exactly those from `from` ms
// up to `until` ms are held.
template <std::size_t N>
static void check_held_between(const Sample (&samples)[N], const uint8_t hold, const double from,
                               const double until = 1e9) {
    SnapbackStick s;
    for (const auto &p : samples) {
        const bool held = step(s, p, hold);
        const bool expect = p.t >= from && p.t < until;
        if (held != expect) std::printf("  t=%.2f held=%d\n", p.t, held);
        CHECK(held == expect);
    }
}

static void test_off_passes_through() {
    SnapbackStick s;
    for (const auto &p : release) CHECK(!step(s, p, 0));
}

static void test_release_is_held() {
    // Enters the 15% zone at 4 ms; the hold lasts 30 ms from there.
    check_held_between(release, 30, 4, 34);
}

static void test_short_hold_ends_early() {
    check_held_between(release, 10, 4, 14);
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
    // Never reaches 35%: no release to filter.
    SnapbackStick s;
    const Sample small[] = {{0, 0.3, 0}, {2, 0.2, 0}, {4, 0.05, 0}, {6, -0.12, 0}, {8, 0.0, 0}};
    for (const auto &p : small) CHECK(!step(s, p, 30));
}

static void test_partial_release_is_held() {
    // Let go from 45%: the aftermarket stick still overshoots by a third of that.
    const Sample partial[] = {{0, 0.45, 0}, {2, 0.25, 0}, {4, 0.05, 0}, {6, -0.15, 0}, {8, -0.1, 0}, {10, 0.02, 0}};
    check_held_between(partial, 30, 4);
}

static void test_reversal_goes_through() {
    // The thumb moves the stick across centre fast enough to look like a
    // release, and stops 12% past it. Once it has stayed there for 10 ms, the
    // hold ends.
    const Sample across[] = {{0, 1.0, 0},    {2, 0.6, 0},    {4, 0.3, 0},    {6, 0.1, 0},    {8, -0.05, 0},
                             {10, -0.1, 0},  {12, -0.12, 0}, {14, -0.12, 0}, {16, -0.12, 0}, {18, -0.12, 0},
                             {20, -0.12, 0}};
    // Last drop at 8 ms, so the stick has stayed put long enough at 18 ms.
    check_held_between(across, 55, 6, 18);
}

static void test_rise_restarts_after_gap() {
    // A gap in the reports while holding does not count as the stick staying
    // put, since it may have bounced in between.
    const Sample s[] = {{0, 1.0, 0},    {2, 0.5, 0},    {4, 0.1, 0},    {6, -0.1, 0},   {8, -0.2, 0},
                        {28, -0.2, 0},  {30, -0.2, 0},  {34, -0.2, 0},  {38, -0.2, 0},  {40, -0.2, 0}};
    check_held_between(s, 55, 4, 38);
}

static void test_settle_restarts_after_gap() {
    // The stick is in the 3% zone at 6 ms, a dropout hides the overshoot, and
    // it is back in the zone at 18 ms. Settling starts over there, so the 30%
    // rebound after it stays held. Counted from 6 ms, the hold would end at 18.
    const Sample s[] = {{0, 1.0, 0},  {2, 0.5, 0},  {4, 0.1, 0},  {6, 0.01, 0}, {18, 0.01, 0}, {20, 0.3, 0},
                        {22, 0.2, 0}, {24, 0.05, 0}, {26, 0.0, 0}, {28, 0.0, 0}, {30, 0.0, 0},  {34, 0.0, 0}};
    // Settled within 3% from 26 ms, so the hold ends at 34.
    check_held_between(s, 55, 4, 34);
}

static void test_peak_follows_slow_ease() {
    // Full push, eased back to 45% and aimed there, then let go and pushed
    // 70% the other way. That is past 80% of the 45% push it was let go from,
    // so it goes through at once. Judged against the earlier full push it
    // would have been held. Reports come every 2 ms, since a longer gap keeps
    // the peak.
    SnapbackStick s;
    for (int t = 0; t <= 50; t += 2) {
        const double x = t < 30 ? 1.0 - 0.55 * t / 30.0 : 0.45;
        CHECK(!step(s, {static_cast<double>(t), x, 0}, 30));
    }
    CHECK(!step(s, {52, 0.2, 0}, 30));
    CHECK(step(s, {54, 0.02, 0}, 30));
    CHECK(!step(s, {56, -0.7, 0}, 30));
    CHECK(!step(s, {58, -0.7, 0}, 30));
}

static void test_peak_kept_across_dropout() {
    // Full push, then 20 ms without reports, and the first report back is
    // mid-release at 50%. The gap does not age the full push's peak, so the
    // 60% overshoot stays below 80% of it and is held.
    const Sample s[] = {{-10, 1.0, 0}, {0, 1.0, 0},   {20, 0.5, 0},  {22, 0.05, 0},
                        {24, -0.6, 0}, {26, -0.3, 0}, {28, 0.1, 0},  {30, 0.15, 0}};
    check_held_between(s, 30, 22);
}

static void test_hard_push_escapes() {
    // Let go, then flick hard the other way 8 ms later: that goes through.
    SnapbackStick s;
    const Sample flick[] = {{0, 1.0, 0}, {2, 0.4, 0}, {4, 0.1, 0}, {6, -0.5, 0}, {8, -1.0, 0}, {10, -1.0, 0}};
    const bool expect[] = {false, false, true, true, false, false};
    for (int i = 0; i < 6; i++) CHECK(step(s, flick[i], 30) == expect[i]);
    // Letting go of that flick is filtered again, here from its 60% overshoot.
    CHECK(step(s, {12, 0.6, 0}, 30));
    CHECK(step(s, {14, -0.05, 0}, 30));
}

static void test_overshoot_after_dropout() {
    // Recorded right stick release (55 ms results, release 63): no reports for
    // 14 ms around the release, and the first one after is 67% past centre.
    // That is the overshoot, not a new push, so it is held.
    const Sample s[] = {{-18.89, 1.0, 0}, {-13.85, 1.0, 0}, {0, -0.674, 0.01}, {16.37, 0.0, 0}, {20.14, 0.0, 0}};
    check_held_between(s, 55, 0);
    // A full flick across goes through, and so does a 60% push to the side.
    const Sample flick[] = {{0, 1.0, 0}, {2, -1.0, 0}, {4, -1.0, 0}};
    check_held_between(flick, 55, 1e9);
    const Sample side[] = {{0, 1.0, 0}, {2, -0.3, 0.6}, {4, -0.3, 0.6}};
    check_held_between(side, 55, 1e9);
}

static void test_peak_starts_over_on_other_side() {
    // Full push right, then reports stop while the thumb slams across, and
    // the next one shows 85% left. That push is let go, and 72% right during
    // the hold is past 80% of the 85% push, so it goes through. Judged
    // against the earlier full push it would have been held.
    const Sample s[] = {{0, 1.0, 0},    {2, 1.0, 0},     {20, -0.85, 0},
                        {22, -0.85, 0}, {24, -0.3, 0},   {26, -0.05, 0}, {28, 0.72, 0}};
    check_held_between(s, 55, 26, 28);
}

static void test_jump_past_centre() {
    // With a report every 4 ms the stick can go from 60% straight to 40% past
    // centre without a report near centre.
    const Sample jump[] = {{0, 1.0, 0}, {4, 0.6, 0}, {8, -0.4, 0.1}, {12, 0.2, 0}, {16, 0.0, 0}};
    check_held_between(jump, 30, 8);
}

static void test_sideways_move_is_not_held() {
    // Swinging the stick from right to up keeps it far from centre along the
    // first direction only briefly; it never comes back to centre, so no hold.
    SnapbackStick s;
    const Sample swing[] = {{0, 1.0, 0}, {4, 0.7, 0.7}, {8, 0.0, 1.0}, {12, 0.0, 1.0}};
    for (const auto &p : swing) CHECK(!step(s, p, 30));
}

static void test_diagonal() {
    const Sample diag[] = {{0, 0.7, 0.7}, {2, 0.3, 0.3}, {4, 0.05, 0.05}, {6, -0.3, -0.3}};
    check_held_between(diag, 30, 4);
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
    check_held_between(s, 30, 4, 8);
}

static void test_slow_decaying_bounce_stays_held() {
    // Recorded right TMR stick release (55 ms results, release 39), with the
    // overshoot only caught at 25% because reports were missing around its
    // peak. The 46% rebound after it is still the bounce and stays held.
    const Sample s[] = {
        {-3, 1.0, 0},       {0, 0.706, 0},      {1.26, 0.526, 0},   {3.78, 0.05, 0},   {7, -0.25, 0},
        {11.33, 0.455, 0},  {20.15, 0.392, 0},  {21.4, 0.267, 0},   {23.92, -0.243, 0}, {26.44, -0.4, 0},
        {27.7, -0.384, 0},  {30.22, -0.039, 0}, {31.48, 0.071, 0},  {33.99, 0.251, 0}, {36.51, 0.086, 0},
    };
    check_held_between(s, 55, 3);
}

static void test_slow_push_escapes() {
    // Let go, then pushed the other way slowly: past 5% and still growing
    // 10 ms after the release, so it goes through.
    const Sample s[] = {{0, 1.0, 0},   {2, 0.4, 0},   {4, 0.1, 0},   {6, -0.15, 0}, {8, -0.2, 0},
                        {10, -0.25, 0}, {12, -0.3, 0}, {14, -0.35, 0}, {16, -0.4, 0}};
    check_held_between(s, 30, 4, 14);
}

// Up to `amount` counts of repeatable jitter on a raw axis.
static uint8_t jitter(const uint8_t raw, const int amount, uint32_t &seed) {
    if (amount == 0) return raw;
    seed = seed * 1664525u + 1013904223u;
    const int v = raw + static_cast<int>((seed >> 16) % (2 * amount + 1)) - amount;
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

static void test_release_across_dropout() {
    // Recorded right stick release (results release 18): no reports for about
    // 25 ms around the release, and the stick comes back mid-bounce at 24%.
    // The gap does not count toward the 15 ms window, so the first crossing
    // at 8 ms starts the hold and the 34% bounce after it is hidden.
    const Sample s[] = {
        {-25, 1.0, 0},       {0, 0.245, -0.012},  {4.41, 0.328, 0.052}, {5.67, 0.175, 0.040},
        {8.19, -0.133, 0.016}, {9.44, -0.342, -0.042}, {11.96, -0.304, -0.049}, {14.48, 0.023, -0.004},
        {15.74, 0.154, 0.012}, {18.26, 0.206, 0.035}, {19.52, 0.043, 0.016}, {22.04, -0.121, -0.010},
        {24.56, -0.058, -0.013}, {28.33, 0.095, -0.001}, {32.11, -0.071, 0.005}, {35.89, 0.039, -0.007},
    };
    check_held_between(s, 55, 8);
    // A stick that was already let go slowly before the gap stays unheld.
    SnapbackStick slow;
    const Sample before[] = {{0, 1.0, 0}, {6, 0.6, 0}, {12, 0.34, 0}, {18, 0.3, 0}, {24, 0.25, 0}};
    for (const auto &p : before) CHECK(!step(slow, p, 55));
    CHECK(!step(slow, {51, -0.3, 0}, 55));
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
    test_partial_release_is_held();
    test_reversal_goes_through();
    test_rise_restarts_after_gap();
    test_settle_restarts_after_gap();
    test_peak_follows_slow_ease();
    test_peak_kept_across_dropout();
    test_hard_push_escapes();
    test_peak_starts_over_on_other_side();
    test_jump_past_centre();
    test_sideways_move_is_not_held();
    test_diagonal();
    test_timestamp_wrap();
    test_repeated_releases();
    test_settle_ends_hold_early();
    test_sideways_push_escapes();
    test_slow_decaying_bounce_stays_held();
    test_overshoot_after_dropout();
    test_slow_push_escapes();
    test_modelled_bounces_stay_held();
    test_release_across_dropout();
    test_apply_centres_only_that_stick();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("snapback filter: all tests passed\n");
    return 0;
}
