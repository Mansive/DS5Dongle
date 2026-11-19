# Snapback filter

Status: implemented in firmware and `tools/config_tool.py`. The web UI
(`ds5dongle-config-web`) does not have the settings yet. The host tests pass,
and the filter was replayed against recorded releases of a stock DualSense Edge
stick and a Besavior JS13 Pro+ stick. It has **not** been tested on hardware
yet.

## What it does

A stick that is flicked and let go springs back, overshoots centre and bounces
for a few tens of milliseconds. Measured on a DualSense Edge over USB, the stock
stick overshot up to 54% of full travel and settled in about 26 ms. The JS13
overshot up to 72% and took about 38 ms. A game with a 10% deadzone reads most
of those bounces as input.

The filter watches each stick. When it sees a release, it reports that stick at
centre (`0x80`, `0x80`) for the configured time and then passes the stick
through again.

## Settings

| Setting | Values | Default |
|---------|--------|---------|
| `snapback_left` | `0`–`60` ms the left stick is held at centre after a release, `0` off | `0` |
| `snapback_right` | same for the right stick | `0` |

```sh
python tools/config_tool.py set snapback_left=20 snapback_right=36
```

The two bytes follow `touchpad_sensitivity` at the end of `Config_body`
(offsets 27 and 28). Flash written by older firmware reads `0xFF` there, which
is out of range and turns the filter off. Config tools that send a shorter body
leave both settings unchanged.

## Choosing the hold time

Run [the snapback tester](../tools/snapback-test.html) with WebHID, set its
deadzone to the game's deadzone, and record 20 or more releases of each stick
in different directions. Use its **Filter hold needed** figure. That figure is
the time from the stick first coming back to the deadzone until it stops leaving
it, plus one reading.

Replaying the recorded releases through the firmware filter gave these results
at a 10% deadzone.

| Stick | Hold | Releases caught | Bounces left past 10% |
|-------|------|-----------------|-----------------------|
| Stock | 19 ms | 50 of 50 | 0 |
| JS13 | 35 ms | 50 of 50 | 0 |
| JS13 | 25 ms | 50 of 50 | 21 |

Spacing the same recordings 4 ms apart, like a slower Bluetooth link, gave the
same result except for one JS13 release that a finger slowed down. It bounced
only 10%, and the filter rightly left it alone.

A longer hold costs nothing while the stick is resting. It only delays a
deliberate move that starts within the hold, and only if that move stays under
90%.

## How a release is detected

All thresholds are fractions of full travel, measured from `0x80`. They are in
`src/snapback_filter.h`.

1. **Armed.** The stick is at 50% or further. Its position is remembered.
2. **Released.** Within 15 ms of last being armed, the stick's position along
   the remembered direction comes back to 15% or less. It does not have to land
   near centre. The spring often carries it straight past centre between two
   reports. The spring covers this distance in 2–4 ms, so a slower return is
   the player moving the stick and is not held.
3. **Holding.** The stick is reported at centre until the hold time has passed
   since the release. A push of 90% or more ends the hold at once. Measured
   bounces stay well below that.
4. When the hold ends the stick passes through again, and a new push of 50% or
   more arms it for the next release.

Time comes from the controller's `SensorTimestamp` (units of 1/3 µs), so the
hold does not depend on when Bluetooth packets arrive. The counter wraps every
24 minutes, and the unsigned arithmetic handles that. The state is cleared when
the controller disconnects.

The filter runs on every controller report in `on_bt_data`, before any USB
report is built, so it works the same in every polling mode. Only the stick
bytes change. Buttons, triggers, touch and motion are untouched.

## Tests

```sh
python tests/test_snapback_filter.py
```

This builds `tests/snapback_filter_test.cpp` with `g++` against
`src/snapback_filter.cpp` and checks the config layout against
`tools/config_tool.py`.

## Open items

- Hardware test over Bluetooth with both sticks, in a game with a known
  deadzone.
- Settings in the web UI.
