#include "touchpad_mouse.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "button_remap.h"
#include "config.h"
#include "utils.h"

// Base gain per sensitivity step: the default (8) moves the pointer about
// 770 counts across the full touchpad width when moving slowly.
static constexpr float GAIN_PER_STEP = 0.05f;
// Mild acceleration: faster strokes scale up to 1 + ACCEL_MAX_BOOST times.
static constexpr float ACCEL_SPEED_FULL = 48.0f; // touchpad units per report
static constexpr float ACCEL_MAX_BOOST = 1.5f;

void touchpad_motion_step(TouchpadMotion &motion, const bool touching, const uint8_t index, const uint16_t x,
                          const uint16_t y, const uint8_t sensitivity, int32_t &dx, int32_t &dy) {
    dx = 0;
    dy = 0;
    if (!touching) {
        motion = {};
        return;
    }
    if (!motion.tracking || motion.index != index) {
        motion = {};
        motion.tracking = true;
        motion.index = index;
        motion.x = x;
        motion.y = y;
        return;
    }
    const int32_t raw_x = static_cast<int32_t>(x) - motion.x;
    const int32_t raw_y = static_cast<int32_t>(y) - motion.y;
    motion.x = x;
    motion.y = y;
    if (std::abs(raw_x) > TOUCHPAD_JUMP_LIMIT || std::abs(raw_y) > TOUCHPAD_JUMP_LIMIT) {
        motion.rem_x = motion.rem_y = 0.0f;
        return;
    }
    if (raw_x == 0 && raw_y == 0) {
        return;
    }
    const float speed = std::sqrt(static_cast<float>(raw_x * raw_x + raw_y * raw_y));
    const float boost = 1.0f + ACCEL_MAX_BOOST * std::fmin(speed, ACCEL_SPEED_FULL) / ACCEL_SPEED_FULL;
    const float gain = static_cast<float>(sensitivity) * GAIN_PER_STEP * boost;
    const float fx = static_cast<float>(raw_x) * gain + motion.rem_x;
    const float fy = static_cast<float>(raw_y) * gain + motion.rem_y;
    dx = static_cast<int32_t>(fx); // Truncates toward zero; the rest carries over.
    dy = static_cast<int32_t>(fy);
    motion.rem_x = fx - static_cast<float>(dx);
    motion.rem_y = fy - static_cast<float>(dy);
}

void touchpad_hide_touch(USBGetStateData &state) {
    for (auto &finger : state.Touch.Finger) {
        finger.NotTouching = 1;
    }
}

#ifdef ENABLE_WAKE_HID

#include "tusb.h"
#include "usb.h"

static constexpr uint8_t CONSUMER_INSTANCE = 2;
static constexpr uint8_t MOUSE_REPORT_ID = 3;
static constexpr int32_t PENDING_LIMIT = 100000; // Movement kept while the endpoint is busy

static TouchpadMotion motion;
static int32_t pending_x = 0;
static int32_t pending_y = 0;
static uint8_t buttons = 0;
static uint8_t sent_buttons = 0;
static bool runtime_enabled = true;

static bool mouse_available() {
    return !usb_keyboard_only && !usb_reconfiguring && tud_mounted() && !tud_suspended();
}

static int32_t clamp_i32(const int32_t value, const int32_t limit) {
    return value > limit ? limit : (value < -limit ? -limit : value);
}

bool touchpad_mouse_active() {
    return runtime_enabled && get_config().touchpad_mouse != TouchpadMouseOff;
}

bool touchpad_mouse_hides_touch() {
    return runtime_enabled && get_config().touchpad_mouse == TouchpadMouseHideTouch;
}

void touchpad_mouse_reset() {
    motion = {};
    pending_x = pending_y = 0;
    buttons = 0;
    // sent_buttons is kept: a press the host has seen still needs its release.
}

void touchpad_mouse_toggle() {
    runtime_enabled = !runtime_enabled;
    motion = {};
    pending_x = pending_y = 0;
    printf("[Touchpad] Mouse %s\n", runtime_enabled ? "on" : "off");
}

void touchpad_mouse_on_report(const USBGetStateData &state) {
    // Mouse buttons come from the remap tables (base and layers) on the
    // physical state, the same way the gamepad report gets its buttons.
    USBGetStateData remapped = state;
    button_remap_apply(remapped);
    buttons = button_remap_mouse_buttons();

    if (touchpad_mouse_active()) {
        const auto &finger = state.Touch.Finger[0];
        int32_t dx, dy;
        touchpad_motion_step(motion, !finger.NotTouching, finger.Index, finger.FingerX, finger.FingerY,
                             get_config().touchpad_sensitivity, dx, dy);
        pending_x = clamp_i32(pending_x + dx, PENDING_LIMIT);
        pending_y = clamp_i32(pending_y + dy, PENDING_LIMIT);
    } else {
        motion = {};
        pending_x = pending_y = 0;
    }
    touchpad_mouse_task();
}

void touchpad_mouse_task() {
    if (!mouse_available()) {
        // The host forgets mouse state on suspend and re-enumeration.
        motion = {};
        pending_x = pending_y = 0;
        sent_buttons = 0;
        return;
    }
    if (pending_x == 0 && pending_y == 0 && buttons == sent_buttons) return;
    if (!tud_hid_n_ready(CONSUMER_INSTANCE)) return;

    const auto x = static_cast<int16_t>(clamp_i32(pending_x, 32767));
    const auto y = static_cast<int16_t>(clamp_i32(pending_y, 32767));
    const uint8_t report[5] = {
        buttons,
        static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>((x >> 8) & 0xFF),
        static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>((y >> 8) & 0xFF),
    };
    if (tud_hid_n_report(CONSUMER_INSTANCE, MOUSE_REPORT_ID, report, sizeof(report))) {
        pending_x -= x;
        pending_y -= y;
        sent_buttons = buttons;
    }
}

#endif // ENABLE_WAKE_HID
