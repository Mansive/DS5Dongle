#pragma once

#include <cstdint>

struct USBGetStateData;
struct TouchData;
struct TouchFingerData;

// Touchpad-as-mouse (see docs/touchpad-mouse.md). The pointer and the mouse
// buttons that the remap tables produce go out as Report ID 3 on the consumer
// HID interface.

// Pointer tracking for one finger. Kept free of USB so host tests can drive
// it directly.
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

// The finger that moves the pointer: the tracked one while it stays down,
// otherwise the first finger touching, or nullptr when none is. Lifting the
// tracked finger hands over to a finger still down, and a new touch never
// takes over from the tracked finger.
const TouchFingerData *touchpad_pick_finger(const TouchpadMotion &motion, const TouchData &touch);

// Feeds one report's tracked finger and returns the pointer movement in counts.
// Movement starts on the second report of a contact, so touching down never
// moves the pointer.
void touchpad_motion_step(TouchpadMotion &motion, bool touching, uint8_t index, uint16_t x, uint16_t y,
                          uint8_t sensitivity, int32_t &dx, int32_t &dy);

// The toggle shortcut's pause. It lasts until the next toggle, a reboot or a
// change of the configured mode, so turning the mode off and on again in the
// config never leaves the touchpad mouse paused.
struct TouchpadPause {
    bool paused = false;
    uint8_t mode = 0; // Configured mode when the pause started
};
// Flips the pause. Returns false, changing nothing, while the mode is off.
bool touchpad_pause_toggle(TouchpadPause &pause, uint8_t mode);
// Ends the pause if the configured mode changed. Returns whether it still applies.
bool touchpad_pause_update(TouchpadPause &pause, uint8_t mode);

// Marks both fingers as not touching, so the game sees an untouched touchpad.
void touchpad_hide_touch(USBGetStateData &state);

#ifdef ENABLE_WAKE_HID
// On, not turned off by the toggle shortcut, and set to hide the touchpad from the game.
bool touchpad_mouse_hides_touch();
// Called with every controller input report (physical state).
void touchpad_mouse_on_report(const USBGetStateData &state);
// Retries a mouse report the endpoint was too busy to take.
void touchpad_mouse_task();
void touchpad_mouse_reset();
// The toggle shortcut: pauses or resumes the touchpad mouse (see TouchpadPause).
void touchpad_mouse_toggle();
#else
static inline bool touchpad_mouse_hides_touch() { return false; }
static inline void touchpad_mouse_on_report(const USBGetStateData &) {}
static inline void touchpad_mouse_task() {}
static inline void touchpad_mouse_reset() {}
static inline void touchpad_mouse_toggle() {}
#endif
