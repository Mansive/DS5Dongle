#include "snapback_filter.h"

#include "utils.h"

namespace {

constexpr int32_t STICK_CENTRE = 0x80;
constexpr int32_t STICK_RANGE = 127;

constexpr int32_t radius(const uint8_t percent) {
    return STICK_RANGE * percent / 100;
}

constexpr int32_t ARM_R = radius(SNAPBACK_ARM_PERCENT);
constexpr int32_t ENTER_R = radius(SNAPBACK_ENTER_PERCENT);
constexpr int32_t SETTLE_R = radius(SNAPBACK_SETTLE_PERCENT);
constexpr int32_t PUSH_R = radius(SNAPBACK_PUSH_PERCENT);
constexpr int32_t SIDEWAYS_R = radius(SNAPBACK_SIDEWAYS_PERCENT);
constexpr int32_t ENTER_SQ = ENTER_R * ENTER_R;
constexpr int32_t SIDEWAYS_SQ = SIDEWAYS_R * SIDEWAYS_R;
// A deflection that drops by no more than this still counts as staying put.
constexpr int32_t RISE_NOISE = 2;
constexpr uint32_t RELEASE_TICKS = SNAPBACK_RELEASE_MS * SNAPBACK_TICKS_PER_MS;
constexpr uint32_t GAP_TICKS = SNAPBACK_GAP_MS * SNAPBACK_TICKS_PER_MS;
constexpr uint32_t SETTLE_TICKS = SNAPBACK_SETTLE_MS * SNAPBACK_TICKS_PER_MS;
constexpr uint32_t RISE_TICKS = SNAPBACK_RISE_MS * SNAPBACK_TICKS_PER_MS;

// All products below stay in 32 bits, since deflections are at most 181. The
// RP2040 has no 64-bit multiply instruction, and its library routine runs from
// flash.
static_assert(ARM_R < 64, "back_at_centre bounds assume at most a 50% arm radius");

// Integer square root of a squared deflection (at most 2 * 128 * 128).
uint32_t __not_in_flash_func(deflection)(uint32_t sq) {
    uint32_t root = 0;
    uint32_t bit = 1u << 14;
    while (bit > sq) bit >>= 2;
    while (bit != 0) {
        if (sq >= root + bit) {
            sq -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

int32_t arm_sq(const SnapbackStick &stick) {
    return stick.arm_x * stick.arm_x + stick.arm_y * stick.arm_y;
}

// Position along the direction the stick was pushed, scaled by |arm|.
int32_t along(const SnapbackStick &stick, const int32_t dx, const int32_t dy) {
    return dx * stick.arm_x + dy * stick.arm_y;
}

// True once the stick's position along the direction it was pushed is within
// SNAPBACK_ENTER_PERCENT of centre or past it. Called only below ARM_R.
bool back_at_centre(const SnapbackStick &stick, const int32_t dx, const int32_t dy) {
    const int32_t a = along(stick, dx, dy);
    if (a <= 0) return true;
    // a / |arm| <= enter, squared to stay in integers.
    return a * a <= ENTER_SQ * arm_sq(stick);
}

// True when the stick is past SNAPBACK_ESCAPE_PERCENT of the push it was let go
// from, which makes it a new push rather than a bounce.
bool escaped(const SnapbackStick &stick, const int32_t r) {
    return r * 100 > stick.peak_r * SNAPBACK_ESCAPE_PERCENT;
}

// True when the stick is SNAPBACK_SIDEWAYS_PERCENT or more off the line it was
// pushed along.
bool sideways(const SnapbackStick &stick, const int32_t dx, const int32_t dy) {
    const int32_t cross = dx * stick.arm_y - dy * stick.arm_x;
    return cross * cross >= SIDEWAYS_SQ * arm_sq(stick);
}

void start_hold(SnapbackStick &stick, const int32_t r, const uint32_t timestamp) {
    stick.armed = false;
    stick.holding = true;
    stick.hold_from = timestamp;
    stick.prev_r = static_cast<uint8_t>(r);
    stick.rise_from = timestamp;
    stick.settling = r < SETTLE_R;
    stick.settle_from = timestamp;
}

// One report while holding. False when the hold ends, so the report passes.
bool __not_in_flash_func(keep_holding)(SnapbackStick &stick, const int32_t dx, const int32_t dy, const int32_t r,
                                       const uint32_t timestamp, const uint8_t hold_ms) {
    // Unsigned subtraction keeps working across the timestamp wrap.
    if (timestamp - stick.hold_from >= hold_ms * SNAPBACK_TICKS_PER_MS) return false;
    if (escaped(stick, r)) return false;

    // Settled near centre.
    if (r < SETTLE_R) {
        if (!stick.settling) {
            stick.settling = true;
            stick.settle_from = timestamp;
        } else if (timestamp - stick.settle_from >= SETTLE_TICKS) {
            return false;
        }
    } else {
        stick.settling = false;
    }

    // Sideways from the line the stick bounces along.
    if (sideways(stick, dx, dy)) return false;

    // A deflection that grows or stays put.
    if (r + RISE_NOISE < stick.prev_r) stick.rise_from = timestamp;
    stick.prev_r = static_cast<uint8_t>(r);
    if (r >= PUSH_R && timestamp - stick.rise_from >= RISE_TICKS) return false;

    return true;
}

void centre(uint8_t &x, uint8_t &y) {
    x = STICK_CENTRE;
    y = STICK_CENTRE;
}

} // namespace

SnapbackFilter snapback_filter;

void snapback_filter_reset() {
    snapback_filter = {};
}

// The step and apply functions run in RAM on every controller report, like
// on_bt_data that calls them, so an XIP cache miss never delays the report.
bool __not_in_flash_func(snapback_step)(SnapbackStick &stick, const uint8_t x, const uint8_t y,
                                        const uint32_t timestamp, const uint8_t hold_ms) {
    if (hold_ms == 0) {
        stick = {};
        return false;
    }
    const int32_t dx = x - STICK_CENTRE;
    const int32_t dy = y - STICK_CENTRE;
    const auto r = static_cast<int32_t>(deflection(static_cast<uint32_t>(dx * dx + dy * dy)));

    // A gap in the reports (a dropped link) hides what the stick did during it.
    const uint32_t since_last = timestamp - stick.last_at;
    stick.last_at = timestamp;
    if (since_last > GAP_TICKS) {
        // It can come back already past centre, so leave the gap out of the
        // release window and that release is still caught.
        if (stick.armed) stick.armed_at += since_last;
        // It may have bounced, so a steady push starts over.
        if (stick.holding) stick.rise_from = timestamp;
    }

    if (stick.holding) {
        if (keep_holding(stick, dx, dy, r, timestamp, hold_ms)) return true;
        stick.holding = false;
        stick.armed = false;
    }

    if (stick.armed && timestamp - stick.armed_at > RELEASE_TICKS) {
        stick.armed = false;
    }
    if (r >= ARM_R) {
        // Far past centre on the other side, straight after a push: the
        // spring's overshoot, seen when reports stopped while it crossed
        // centre. A push past SNAPBACK_ESCAPE_PERCENT of the last one counts
        // as a new one.
        if (stick.armed && !escaped(stick, r) && along(stick, dx, dy) < 0 && !sideways(stick, dx, dy)) {
            start_hold(stick, r, timestamp);
            return true;
        }
        if (!stick.armed || r > stick.peak_r) stick.peak_r = static_cast<uint8_t>(r);
        stick.armed = true;
        stick.armed_at = timestamp;
        stick.arm_x = static_cast<int16_t>(dx);
        stick.arm_y = static_cast<int16_t>(dy);
        return false;
    }
    if (stick.armed && back_at_centre(stick, dx, dy)) {
        start_hold(stick, r, timestamp);
        return true;
    }
    return false;
}

void __not_in_flash_func(snapback_filter_apply)(SnapbackFilter &filter, USBGetStateData &state,
                                                const uint8_t left_ms, const uint8_t right_ms) {
    const uint32_t timestamp = state.SensorTimestamp;
    if (snapback_step(filter.left, state.LeftStickX, state.LeftStickY, timestamp, left_ms)) {
        centre(state.LeftStickX, state.LeftStickY);
    }
    if (snapback_step(filter.right, state.RightStickX, state.RightStickY, timestamp, right_ms)) {
        centre(state.RightStickX, state.RightStickY);
    }
}
