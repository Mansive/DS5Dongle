#pragma once

#include <cstdint>

struct USBGetStateData;

// Touchpad-as-mouse (see docs/touchpad-mouse.md). The pointer and the mouse
// buttons that the remap tables produce go out as Report ID 3 on the consumer
// HID interface.

// Pointer tracking for the first finger. Kept free of USB so host tests can
// drive it directly.
struct TouchpadMotion {
    bool tracking = false;
    uint8_t index = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    float rem_x = 0.0f; // Fractional counts carried to the next report
    float rem_y = 0.0f;
};

// A finger that moves further than this between two reports is treated as a
// new contact, not as movement (touchpad units, about 1920 x 1080 in total).
constexpr int32_t TOUCHPAD_JUMP_LIMIT = 400;

// Feeds one report's first finger and returns the pointer movement in counts.
// Movement starts on the second report of a contact, so touching down never
// moves the pointer.
void touchpad_motion_step(TouchpadMotion &motion, bool touching, uint8_t index, uint16_t x, uint16_t y,
                          uint8_t sensitivity, int32_t &dx, int32_t &dy);

// Marks both fingers as not touching, so the game sees an untouched touchpad.
void touchpad_hide_touch(USBGetStateData &state);

#ifdef ENABLE_WAKE_HID
// Configured on and not turned off by the toggle shortcut.
bool touchpad_mouse_active();
// Active and set to hide the touchpad from the game.
bool touchpad_mouse_hides_touch();
// Called with every controller input report (physical state).
void touchpad_mouse_on_report(const USBGetStateData &state);
// Retries a mouse report the endpoint was too busy to take.
void touchpad_mouse_task();
void touchpad_mouse_reset();
// The toggle shortcut: turns the touchpad mouse off or back on until reboot.
void touchpad_mouse_toggle();
#else
static inline bool touchpad_mouse_active() { return false; }
static inline bool touchpad_mouse_hides_touch() { return false; }
static inline void touchpad_mouse_on_report(const USBGetStateData &) {}
static inline void touchpad_mouse_task() {}
static inline void touchpad_mouse_reset() {}
static inline void touchpad_mouse_toggle() {}
#endif
