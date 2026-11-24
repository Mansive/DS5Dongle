#pragma once

#include <cstdint>

struct USBGetStateData;

// Snapback filter (see docs/snapback-filter.md). When a stick is let go from a
// large deflection, its spring throws it past centre and it bounces for a few
// tens of milliseconds. The filter spots that release and reports the stick at
// centre until the bounce settles, at most for the configured time, so the
// game never sees the bounce. Movement that does not look like a bounce ends
// the hold at once.

// A release starts from at least this deflection (percent of full travel). A
// stick let go from a third of the way out still overshoots by up to half that
// push on aftermarket sticks...
constexpr uint8_t SNAPBACK_ARM_PERCENT = 35;
// ...and must come back to within this distance of centre, measured along the
// direction it was pushed (the spring can carry it straight past centre between
// two reports, so landing near centre is not required)...
constexpr uint8_t SNAPBACK_ENTER_PERCENT = 15;
// ...within this time of last being at SNAPBACK_ARM_PERCENT. The spring covers
// that distance in a few ms, so this leaves room for reports up to
// SNAPBACK_GAP_MS apart. A slower return is the player moving the stick, not
// the spring.
constexpr uint32_t SNAPBACK_RELEASE_MS = 15;
// A push of at least SNAPBACK_FULL_PERCENT gets the shorter window. The spring
// covers 35% to 15% in 1-6 ms from there, so a slower return from a big push is
// a thumb bringing the stick back.
constexpr uint8_t SNAPBACK_FULL_PERCENT = 80;
constexpr uint32_t SNAPBACK_RELEASE_FULL_MS = 10;
// A gap of more than this between reports (a dropped link) does not count
// toward SNAPBACK_RELEASE_MS, since the release may have happened inside it.
constexpr uint32_t SNAPBACK_GAP_MS = 6;
// While holding, a push past this percent of the release's push still goes
// through at once. The first overshoot measured on stock and aftermarket sticks
// stays below it, at most 76% on a TMR stick. Recorded slams almost always end
// their hold by the rules below first, so the margin costs nothing.
constexpr uint8_t SNAPBACK_ESCAPE_PERCENT = 85;
// The hold ends early once the stick has stayed within SNAPBACK_SETTLE_PERCENT
// of centre for SNAPBACK_SETTLE_MS. A bounce swings through that zone faster
// than that. The zone is small enough for the low deadzones used with TMR
// sticks. A stick that rests further off centre never settles and gets the
// full hold.
constexpr uint8_t SNAPBACK_SETTLE_PERCENT = 3;
constexpr uint32_t SNAPBACK_SETTLE_MS = 8;
// While holding, these also end the hold, since a spring bounce does neither.
// A deflection of at least SNAPBACK_PUSH_PERCENT that grows or stays put for
// SNAPBACK_RISE_MS (a bounce turns around within a few ms, while a thumb that
// moves the stick across centre stops and stays there). The time counts from
// the last drop below the largest deflection since the drop before, so a slow
// sag still counts as a drop. It includes time spent below
// SNAPBACK_PUSH_PERCENT, so a stick
// that rests a little off centre, outside the settle zone, passes through once
// it reaches SNAPBACK_PUSH_PERCENT...
constexpr uint32_t SNAPBACK_RISE_MS = 10;
constexpr uint8_t SNAPBACK_PUSH_PERCENT = 5;
// ...and a move this far sideways from the direction the stick was pushed
// (bounces stay along that line).
constexpr uint8_t SNAPBACK_SIDEWAYS_PERCENT = 35;
// Longest hold the config accepts, in ms (0 turns the filter off).
constexpr uint8_t SNAPBACK_HOLD_MAX_MS = 80;
// SensorTimestamp ticks per millisecond (the counter runs in 1/3 us units).
constexpr uint32_t SNAPBACK_TICKS_PER_MS = 3000;

// Deflections below are in 1/127 of full travel, measured from centre.
struct SnapbackStick {
    uint32_t last_at = 0;     // Timestamp of the previous report
    bool armed = false;       // Was at SNAPBACK_ARM_PERCENT recently
    uint32_t armed_at = 0;    // Timestamp it was last there
    int16_t arm_x = 0;        // Where it was then, relative to centre
    int16_t arm_y = 0;
    uint8_t peak_r = 0;       // Largest deflection of that push in the last 15-30 ms
    uint8_t bucket_max = 0;   // Largest deflection since bucket_at
    uint8_t prev_bucket_max = 0; // Largest deflection in the window before that
    uint32_t bucket_at = 0;   // Timestamp the current window started
    bool holding = false;     // Reporting the stick at centre
    uint32_t hold_from = 0;   // Timestamp the hold started
    // While holding:
    uint8_t prev_r = 0;       // Largest deflection since it last dropped
    uint32_t rise_from = 0;   // Timestamp it last dropped or reports resumed
    bool settling = false;    // Within SNAPBACK_SETTLE_PERCENT of centre
    uint32_t settle_from = 0; // Timestamp it got there
};

// Feeds one report of a stick (raw 0-255 axes, controller timestamp) and
// returns true when the stick should be reported at centre.
bool snapback_step(SnapbackStick &stick, uint8_t x, uint8_t y, uint32_t timestamp, uint8_t hold_ms);

struct SnapbackFilter {
    SnapbackStick left;
    SnapbackStick right;
};

// Applies the filter to both sticks of one controller report in place.
void snapback_filter_apply(SnapbackFilter &filter, USBGetStateData &state, uint8_t left_ms, uint8_t right_ms);

// The filter for the connected controller. Reset it when the controller
// disconnects, since its timestamp starts over on the next connection.
extern SnapbackFilter snapback_filter;
void snapback_filter_reset();
