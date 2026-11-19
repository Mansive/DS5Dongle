#pragma once

#include <cstdint>

struct USBGetStateData;

// Snapback filter (see docs/snapback-filter.md). When a stick is let go from a
// large deflection, its spring throws it past centre and it bounces for a few
// tens of milliseconds. The filter spots that release and reports the stick at
// centre for a set time, so the game never sees the bounce.

// A release starts from at least this deflection (percent of full travel)...
constexpr uint8_t SNAPBACK_ARM_PERCENT = 50;
// ...and must come back to within this distance of centre, measured along the
// direction it was pushed (the spring can carry it straight past centre between
// two reports, so landing near centre is not required)...
constexpr uint8_t SNAPBACK_ENTER_PERCENT = 15;
// ...within this time of last being at SNAPBACK_ARM_PERCENT. The spring covers
// that distance in 2-4 ms, so this leaves room for a report every 4-8 ms. A
// slower return is the player moving the stick, not the spring.
constexpr uint32_t SNAPBACK_RELEASE_MS = 15;
// While holding, a push this far still goes through at once. Bounces measured
// on stock and aftermarket sticks stay well below it.
constexpr uint8_t SNAPBACK_ESCAPE_PERCENT = 90;
// Longest hold the config accepts, in ms (0 turns the filter off).
constexpr uint8_t SNAPBACK_HOLD_MAX_MS = 60;
// SensorTimestamp ticks per millisecond (the counter runs in 1/3 us units).
constexpr uint32_t SNAPBACK_TICKS_PER_MS = 3000;

struct SnapbackStick {
    bool armed = false;     // Was at SNAPBACK_ARM_PERCENT recently
    uint32_t armed_at = 0;  // Timestamp it was last there
    int16_t arm_x = 0;      // Where it was then, relative to centre
    int16_t arm_y = 0;
    bool holding = false;   // Reporting the stick at centre
    uint32_t hold_from = 0; // Timestamp the hold started
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
