# Snapback filter

Status: implemented in firmware, `tools/config_tool.py`, and the web UI
(`ds5dongle-config-web`, branch `feat/touchpad-mouse`). The host tests pass,
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
centre (`0x80`, `0x80`) until the bounce settles, at most for the configured
time, and then passes the stick through again. Movement that a spring bounce
cannot make ends the hold at once.

## Settings

| Setting | Values | Default |
|---------|--------|---------|
| `snapback_left` | `0`–`60` ms the left stick is held at centre at most after a release, `0` off | `0` |
| `snapback_right` | same for the right stick | `0` |

```sh
python tools/config_tool.py set snapback_left=20 snapback_right=36
```

In the web config, both settings are in the **Snapback filter** section of the
General page. The web config needs firmware whose config reply reaches these
two bytes. With older firmware it shows a notice to update the firmware and
locks every setting, leaving only **Reboot to Bootloader** available.

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

The setting is the longest hold. Since the hold ends once the stick settles,
setting it a little above that figure costs little on releases that settle
sooner.

Replaying the recorded releases through the first version of the filter, which
always held for the full time and had only the 90% escape, gave these results
at a 10% deadzone. They have not been replayed through the current rules.

| Stick | Hold | Releases caught | Bounces left past 10% |
|-------|------|-----------------|-----------------------|
| Stock | 19 ms | 50 of 50 | 0 |
| JS13 | 35 ms | 50 of 50 | 0 |
| JS13 | 25 ms | 50 of 50 | 21 |

Spacing the same recordings 4 ms apart, like a slower Bluetooth link, gave the
same result except for one JS13 release that a finger slowed down. It bounced
only 10%, and the filter rightly left it alone.

A longer hold costs nothing while the stick is resting. It only delays a
deliberate move that starts within the hold and that none of the escape rules
below recognise.

## How a release is detected

All thresholds are fractions of full travel, measured from `0x80`. They are in
`src/snapback_filter.h`.

1. **Armed.** The stick is at 50% or further. Its position is remembered.
2. **Released.** Within 15 ms of last being armed, the stick's position along
   the remembered direction comes back to 15% or less. It does not have to land
   near centre. The spring often carries it straight past centre between two
   reports. The spring covers this distance in 2–4 ms, so a slower return is
   the player moving the stick and is not held. A gap of more than 6 ms
   between reports does not count toward the 15 ms. Over Bluetooth, reports
   sometimes stop for 15–25 ms, and when that happens during a release the
   stick comes back already bouncing. A recorded release like that got past
   the filter before this rule.

   A report gap can also hide the return entirely, so the next report shows
   the stick already 50% or more past centre on the other side. Straight after
   a push, that is the spring's overshoot, so it also counts as a release.
   Only a push of 90% or more, or one 35% or more to the side, counts as a new
   push there. A full flick from one side to the other passes, since it
   reaches 90%.
3. **Holding.** The stick is reported at centre until one of these happens.
   - The hold time has passed since the release.
   - **Settled.** The stick has stayed within 3% of centre for 8 ms. A bounce
     swings through that zone faster. The zone suits the low deadzones (about
     0–5%) used with TMR sticks. A stick that rests further than 3% off centre,
     such as a worn potentiometer stick, never settles and gets the full hold.
   - **Hard push.** The stick is at 90% or more. Measured bounces stay well
     below that.
   - **Sideways.** The stick is 35% or more to the side of the line it was
     pushed along. Bounces stay on that line.
   - **Steady push.** The stick is at 25% or more and has not moved back
     towards centre for 10 ms. A bounce turns around within a few ms.
4. When the hold ends the stick passes through again, and a new push of 50% or
   more arms it for the next release.

An earlier version also ended the hold when a swing was larger than the one
before. On a recorded TMR stick, report gaps hid the overshoot peak, so the
next, smaller swing looked larger and the bounce leaked. That rule is gone.
The steady push rule still catches a player who pushes during the hold.

The host tests feed modelled releases through the filter, with the stock
stick's 54% overshoot and the JS13's 72% overshoot and slower swing, sampled
every 1, 2 and 4 ms at every phase, with and without 2 counts of jitter. None
of the escape rules fires on them before the bounce falls under 10%.
Shortening the steady push time to 8 ms makes some of them leak at 4 ms
sampling, so that value has only a small margin. The settle time would leak
at 3 ms. The sideways rule is not covered by these models, since they bounce
along a straight line.

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
- Replay recorded releases (the snapback tester's JSON export) through the
  current rules, to check the settle and escape thresholds against real
  sticks, including bounces after a diagonal release.
