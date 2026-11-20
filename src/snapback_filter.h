#pragma once

#include <cstdint>

struct USBGetStateData;

// Snapback filter (see docs/snapback-filter.md). When a stick is let go from a
// large deflection, its spring throws it past centre and it bounces for a few
// tens of milliseconds. The filter spots that release and reports the stick at
// centre until the bounce settles, at most for the configured time, so the
// game never sees the bounce. Movement that does not look like a bounce ends
// the hold at once.

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
// A gap of more than this between reports (a dropped link) does not count
// toward SNAPBACK_RELEASE_MS, since the release may have happened inside it.
constexpr uint32_t SNAPBACK_GAP_MS = 6;
// While holding, a push this far still goes through at once. Bounces measured
// on stock and aftermarket sticks stay well below it.
constexpr uint8_t SNAPBACK_ESCAPE_PERCENT = 90;
// The hold ends early once the stick has stayed within SNAPBACK_SETTLE_PERCENT
// of centre for SNAPBACK_SETTLE_MS. A bounce swings through that zone faster
// than that. The zone is small enough for the low deadzones used with TMR
// sticks. A stick that rests further off centre never settles and gets the
// full hold.
constexpr uint8_t SNAPBACK_SETTLE_PERCENT = 3;
constexpr uint32_t SNAPBACK_SETTLE_MS = 8;
// While holding, the stick has to be this far from centre to count as being on
// one side, so jitter around centre never starts a new swing.
constexpr uint8_t SNAPBACK_SWING_GATE_PERCENT = 4;
// While holding, these also end the hold, since a spring bounce does none of
// them. Each swing of a bounce is smaller than the one before, so a swing at
// least SNAPBACK_GROWTH_PERCENT larger is the player...
constexpr uint8_t SNAPBACK_GROWTH_PERCENT = 15;
// ...as is a deflection that keeps growing for SNAPBACK_RISE_MS (a bounce
// turns around within a few ms)...
constexpr uint32_t SNAPBACK_RISE_MS = 10;
// ...which both count only from this deflection...
constexpr uint8_t SNAPBACK_PUSH_PERCENT = 25;
// ...and a move this far sideways from the direction the stick was pushed
// (bounces stay along that line).
constexpr uint8_t SNAPBACK_SIDEWAYS_PERCENT = 35;
// Longest hold the config accepts, in ms (0 turns the filter off).
constexpr uint8_t SNAPBACK_HOLD_MAX_MS = 60;
// SensorTimestamp ticks per millisecond (the counter runs in 1/3 us units).
constexpr uint32_t SNAPBACK_TICKS_PER_MS = 3000;

// Deflections below are in 1/127 of full travel, measured from centre.
struct SnapbackStick {
    uint32_t last_at = 0;     // Timestamp of the previous report
    bool armed = false;       // Was at SNAPBACK_ARM_PERCENT recently
    uint8_t arm_peak = 0;     // Largest deflection since it was armed
    uint32_t armed_at = 0;    // Timestamp it was last there
    int16_t arm_x = 0;        // Where it was then, relative to centre
    int16_t arm_y = 0;
    bool holding = false;     // Reporting the stick at centre
    uint32_t hold_from = 0;   // Timestamp the hold started
    // While holding:
    int8_t swing_side = 0;    // 1 on the side it was pushed, -1 past centre
    uint8_t swing_peak = 0;   // Largest deflection of the current swing
    uint8_t last_peak = 0;    // Largest deflection of the swing before
    uint8_t prev_r = 0;       // Deflection in the previous report
    uint32_t rise_from = 0;   // Timestamp the deflection last dropped
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
