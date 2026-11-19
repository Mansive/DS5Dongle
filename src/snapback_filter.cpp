#include "snapback_filter.h"

#include "utils.h"

namespace {

constexpr int32_t STICK_CENTRE = 0x80;
constexpr int32_t STICK_RANGE = 127;

constexpr int32_t radius_sq(const uint8_t percent) {
    const int32_t r = STICK_RANGE * percent / 100;
    return r * r;
}

constexpr int32_t ARM_SQ = radius_sq(SNAPBACK_ARM_PERCENT);
constexpr int32_t ENTER_SQ = radius_sq(SNAPBACK_ENTER_PERCENT);
constexpr int32_t ESCAPE_SQ = radius_sq(SNAPBACK_ESCAPE_PERCENT);
constexpr uint32_t RELEASE_TICKS = SNAPBACK_RELEASE_MS * SNAPBACK_TICKS_PER_MS;

// True once the stick's position along the direction it was pushed is within
// SNAPBACK_ENTER_PERCENT of centre or past it.
bool back_at_centre(const SnapbackStick &stick, const int32_t dx, const int32_t dy) {
    const int64_t along = static_cast<int64_t>(dx) * stick.arm_x + static_cast<int64_t>(dy) * stick.arm_y;
    if (along <= 0) return true;
    const int64_t arm_sq = static_cast<int64_t>(stick.arm_x) * stick.arm_x +
                           static_cast<int64_t>(stick.arm_y) * stick.arm_y;
    // along / |arm| <= enter, squared to stay in integers.
    return along * along <= ENTER_SQ * arm_sq;
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

bool snapback_step(SnapbackStick &stick, const uint8_t x, const uint8_t y, const uint32_t timestamp,
                   const uint8_t hold_ms) {
    if (hold_ms == 0) {
        stick = {};
        return false;
    }
    const int32_t dx = x - STICK_CENTRE;
    const int32_t dy = y - STICK_CENTRE;
    const int32_t r_sq = dx * dx + dy * dy;

    if (stick.holding) {
        // Unsigned subtraction keeps working across the timestamp wrap.
        if (timestamp - stick.hold_from < hold_ms * SNAPBACK_TICKS_PER_MS && r_sq < ESCAPE_SQ) {
            return true;
        }
        stick.holding = false;
        stick.armed = false;
    }

    if (r_sq >= ARM_SQ) {
        stick.armed = true;
        stick.armed_at = timestamp;
        stick.arm_x = static_cast<int16_t>(dx);
        stick.arm_y = static_cast<int16_t>(dy);
        return false;
    }
    if (stick.armed && timestamp - stick.armed_at > RELEASE_TICKS) {
        stick.armed = false;
    }
    if (stick.armed && back_at_centre(stick, dx, dy)) {
        stick.armed = false;
        stick.holding = true;
        stick.hold_from = timestamp;
        return true;
    }
    return false;
}

void snapback_filter_apply(SnapbackFilter &filter, USBGetStateData &state, const uint8_t left_ms,
                           const uint8_t right_ms) {
    const uint32_t timestamp = state.SensorTimestamp;
    if (snapback_step(filter.left, state.LeftStickX, state.LeftStickY, timestamp, left_ms)) {
        centre(state.LeftStickX, state.LeftStickY);
    }
    if (snapback_step(filter.right, state.RightStickX, state.RightStickY, timestamp, right_ms)) {
        centre(state.RightStickX, state.RightStickY);
    }
}
