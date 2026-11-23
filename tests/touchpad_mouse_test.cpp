// Host test for the touchpad mouse: mouse-button remap targets in
// src/button_remap.cpp and the pointer tracking in src/touchpad_mouse.cpp.
// Built by tests/test_touchpad_mouse.py; see that file for the stubs.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "button_remap.h"
#include "button_utils.h"
#include "config.h"
#include "touchpad_mouse.h"
#include "utils.h"

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void reset_tables() {
    std::memset(&storage, 0, sizeof(storage));
    std::memset(&fork_storage, 0, sizeof(fork_storage));
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) {
        storage.button.button_remap[i] = i;
        for (auto &layer : fork_storage.layers.layer) layer.remap[i] = i;
    }
    fork_storage.layers.format = BUTTON_LAYER_FORMAT;
    button_remap_reset();
}

static USBGetStateData idle() {
    USBGetStateData s;
    std::memset(&s, 0, sizeof(s));
    s.DPad = None;
    return s;
}

static void test_base_mouse_targets() {
    reset_tables();
    storage.button.button_remap[ButtonR1] = MouseLeft;
    storage.button.button_remap[ButtonL1] = MouseRight;
    storage.button.button_remap[ButtonPad] = MouseMiddle;
    storage.button.button_remap[ButtonL3] = MouseBack;
    storage.button.button_remap[ButtonR3] = MouseForward;
    CHECK(button_remap_uses_mouse());

    USBGetStateData s = idle();
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0);

    s = idle();
    s.ButtonR1 = 1;
    s.ButtonCross = 1;
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0x01);
    CHECK(s.ButtonR1 == 0);    // A mouse target leaves the gamepad report.
    CHECK(s.ButtonCross == 1); // Unmapped buttons pass through.

    s = idle();
    s.ButtonL1 = s.ButtonPad = s.ButtonL3 = s.ButtonR3 = 1;
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0x1E);
    CHECK(s.ButtonL1 == 0 && s.ButtonPad == 0 && s.ButtonL3 == 0 && s.ButtonR3 == 0);

    // Released: buttons clear, even when nothing else is remapped any more.
    s = idle();
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0);

    // Two sources on one mouse button: held while either is held.
    storage.button.button_remap[ButtonCross] = MouseLeft;
    s = idle();
    s.ButtonCross = 1;
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0x01);
}

static void test_layer_mouse_targets() {
    reset_tables();
    auto &layer = fork_storage.layers.layer[0];
    constexpr uint32_t hold = 1u << (ButtonLeftPaddle - ButtonSquare);
    layer.hold[0] = static_cast<uint8_t>(hold & 0xFF);
    layer.hold[1] = static_cast<uint8_t>((hold >> 8) & 0xFF);
    layer.hold[2] = static_cast<uint8_t>((hold >> 16) & 0xFF);
    layer.remap[ButtonCross] = MouseLeft;
    CHECK(button_remap_uses_mouse());

    // Cross alone: base mapping, no mouse button.
    USBGetStateData s = idle();
    s.ButtonCross = 1;
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0);
    CHECK(s.ButtonCross == 1);

    s = idle();
    button_remap_apply(s);

    // Paddle then Cross: layer mapping clicks the mouse.
    s = idle();
    s.ButtonLeftPaddle = 1;
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0);
    s = idle();
    s.ButtonLeftPaddle = 1;
    s.ButtonCross = 1;
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0x01);
    CHECK(s.ButtonCross == 0 && s.ButtonLeftPaddle == 0);

    // Paddle released first: the latch keeps the click until Cross is up.
    s = idle();
    s.ButtonCross = 1;
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0x01);
    s = idle();
    button_remap_apply(s);
    CHECK(button_remap_mouse_buttons() == 0);
}

static void test_validation() {
    reset_tables();
    fork_storage.layers.layer[1].remap[ButtonSquare] = MouseForward;
    fork_storage.layers.layer[1].remap[ButtonCircle] = RemapTargetCount; // out of range
    button_layer_valid();
    CHECK(fork_storage.layers.layer[1].remap[ButtonSquare] == MouseForward);
    CHECK(fork_storage.layers.layer[1].remap[ButtonCircle] == ButtonCircle);
    reset_tables();
    CHECK(!button_remap_uses_mouse());
}

static void test_motion() {
    TouchpadMotion m;
    int32_t dx = 99, dy = 99;

    // Touch-down never moves the pointer.
    touchpad_motion_step(m, true, 5, 1000, 500, 8, dx, dy);
    CHECK(dx == 0 && dy == 0);
    CHECK(m.tracking);

    // Slow movement: 10 units at sensitivity 8 -> gain 0.4 * (1 + 1.5 * 10/48).
    touchpad_motion_step(m, true, 5, 1010, 500, 8, dx, dy);
    CHECK(dx == 5 && dy == 0); // 10 * 0.525 = 5.25
    // Remainders carry over: four such steps add up to 21 (4 * 5.25), not 20.
    int32_t total = dx;
    for (uint16_t x = 1020; x <= 1040; x += 10) {
        touchpad_motion_step(m, true, 5, x, 500, 8, dx, dy);
        total += dx;
    }
    CHECK(total == 21);

    // Negative movement truncates toward zero and keeps the sign.
    touchpad_motion_step(m, true, 5, 1040, 490, 8, dx, dy);
    CHECK(dx == 0 && dy == -5);

    // Acceleration: a fast stroke moves more than proportionally.
    TouchpadMotion slow, fast;
    int32_t sx = 0, fx = 0, unused = 0;
    touchpad_motion_step(slow, true, 1, 100, 100, 8, unused, unused);
    touchpad_motion_step(slow, true, 1, 110, 100, 8, sx, unused);
    touchpad_motion_step(fast, true, 1, 100, 100, 8, unused, unused);
    touchpad_motion_step(fast, true, 1, 180, 100, 8, fx, unused);
    CHECK(fx > 8 * sx);
    CHECK(fx == 80); // Capped boost: 80 * 0.4 * 2.5

    // Sensitivity scales linearly.
    TouchpadMotion hi;
    int32_t hx = 0;
    touchpad_motion_step(hi, true, 1, 100, 100, 16, unused, unused);
    touchpad_motion_step(hi, true, 1, 110, 100, 16, hx, unused);
    CHECK(hx == 10); // 10 * 0.8 * 1.3125 = 10.5

    // Lift: the next touch starts fresh, wherever it lands.
    touchpad_motion_step(m, false, 5, 0, 0, 8, dx, dy);
    CHECK(dx == 0 && dy == 0 && !m.tracking);
    touchpad_motion_step(m, true, 6, 1800, 1000, 8, dx, dy);
    CHECK(dx == 0 && dy == 0);

    // A new finger index without a lift report is a new contact too.
    touchpad_motion_step(m, true, 7, 200, 200, 8, dx, dy);
    CHECK(dx == 0 && dy == 0);
    CHECK(m.index == 7);

    // A jump larger than the limit is ignored, then tracking continues from there.
    touchpad_motion_step(m, true, 7, 200 + TOUCHPAD_JUMP_LIMIT + 1, 200, 8, dx, dy);
    CHECK(dx == 0 && dy == 0);
    touchpad_motion_step(m, true, 7, 200 + TOUCHPAD_JUMP_LIMIT + 11, 200, 8, dx, dy);
    CHECK(dx == 5);

    // No movement, no output.
    touchpad_motion_step(m, true, 7, 200 + TOUCHPAD_JUMP_LIMIT + 11, 200, 8, dx, dy);
    CHECK(dx == 0 && dy == 0);
}

// Runs one report through finger selection and tracking, like touchpad_mouse_on_report.
static void feed(TouchpadMotion &m, const USBGetStateData &s, int32_t &dx, int32_t &dy) {
    const TouchFingerData *finger = touchpad_pick_finger(m, s.Touch);
    if (finger) {
        touchpad_motion_step(m, true, finger->Index, finger->FingerX, finger->FingerY, 8, dx, dy);
    } else {
        touchpad_motion_step(m, false, 0, 0, 0, 0, dx, dy);
    }
}

static void touch(USBGetStateData &s, int slot, uint8_t index, uint16_t x, uint16_t y) {
    auto &finger = s.Touch.Finger[slot];
    finger.NotTouching = 0;
    finger.Index = index;
    finger.FingerX = x;
    finger.FingerY = y;
}

static void test_finger_handoff() {
    TouchpadMotion m;
    int32_t dx = 0, dy = 0;
    USBGetStateData s = idle();
    s.Touch.Finger[0].NotTouching = s.Touch.Finger[1].NotTouching = 1;

    // Finger A in slot 0 moves the pointer.
    touch(s, 0, 10, 500, 500);
    feed(m, s, dx, dy);
    touch(s, 0, 10, 510, 500);
    feed(m, s, dx, dy);
    CHECK(dx == 5 && m.index == 10);

    // Finger B lands in slot 1 far away. A keeps the pointer.
    touch(s, 1, 11, 1500, 900);
    touch(s, 0, 10, 520, 500);
    feed(m, s, dx, dy);
    CHECK(dx == 5 && m.index == 10);

    // A lifts. B takes over without a jump, then moves the pointer.
    s.Touch.Finger[0].NotTouching = 1;
    feed(m, s, dx, dy);
    CHECK(dx == 0 && dy == 0 && m.index == 11);
    touch(s, 1, 11, 1510, 900);
    feed(m, s, dx, dy);
    CHECK(dx == 5);

    // A third finger in the free slot 0 does not take over from B.
    touch(s, 0, 12, 100, 100);
    touch(s, 1, 11, 1520, 900);
    feed(m, s, dx, dy);
    CHECK(dx == 5 && m.index == 11);

    // Both lift. The next touch starts over, wherever it lands.
    s.Touch.Finger[0].NotTouching = s.Touch.Finger[1].NotTouching = 1;
    feed(m, s, dx, dy);
    CHECK(dx == 0 && dy == 0 && !m.tracking);
    touch(s, 1, 13, 300, 300);
    feed(m, s, dx, dy);
    CHECK(dx == 0 && dy == 0 && m.index == 13);
}

static void test_pause() {
    TouchpadPause p;

    // Nothing to pause while the mode is off.
    CHECK(!touchpad_pause_toggle(p, TouchpadMouseOff));
    CHECK(!touchpad_pause_update(p, TouchpadMouseOn));

    // The toggle pauses and resumes.
    CHECK(touchpad_pause_toggle(p, TouchpadMouseOn));
    CHECK(touchpad_pause_update(p, TouchpadMouseOn));
    CHECK(touchpad_pause_toggle(p, TouchpadMouseOn));
    CHECK(!touchpad_pause_update(p, TouchpadMouseOn));

    // Turning the mode off and on again in the config ends the pause.
    touchpad_pause_toggle(p, TouchpadMouseOn);
    CHECK(!touchpad_pause_update(p, TouchpadMouseOff));
    CHECK(!touchpad_pause_update(p, TouchpadMouseOn));

    // So does a switch to the other on mode.
    touchpad_pause_toggle(p, TouchpadMouseHideTouch);
    CHECK(touchpad_pause_update(p, TouchpadMouseHideTouch));
    CHECK(!touchpad_pause_update(p, TouchpadMouseOn));
}

static void test_hide_touch() {
    USBGetStateData s = idle();
    s.Touch.Finger[0].NotTouching = 0;
    s.Touch.Finger[0].FingerX = 123;
    s.Touch.Finger[1].NotTouching = 0;
    s.Touch.Timestamp = 9;
    touchpad_hide_touch(s);
    CHECK(s.Touch.Finger[0].NotTouching == 1);
    CHECK(s.Touch.Finger[1].NotTouching == 1);
    CHECK(s.Touch.Finger[0].FingerX == 123);
    CHECK(s.Touch.Timestamp == 9);
}

int main() {
    test_base_mouse_targets();
    test_layer_mouse_targets();
    test_validation();
    test_motion();
    test_finger_handoff();
    test_pause();
    test_hide_touch();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("touchpad mouse tests passed\n");
    return 0;
}
