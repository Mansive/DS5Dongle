# Snapback filter

Status: implemented in firmware, `tools/config_tool.py`, and the web UI
(`ds5dongle-config-web`, branch `feat/touchpad-mouse`). The host tests pass,
and the filter was replayed against recorded releases of a stock DualSense Edge
stick and a Besavior JS13 Pro+ stick. On hardware, through the dongle with holds
of 25 ms (left) and 65 ms (right), the snapback tester recorded no overshoot
past centre. It has **not** been tested in a game yet.

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
| `snapback_left` | `0`–`80` ms the left stick is held at centre at most after a release, `0` off | `0` |
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

### Replayed recordings

The current rules were replayed against snapback tester recordings made with
the filter off, on a DualSense Edge with a stock left stick and a TMR right
stick. Each stick was recorded through the dongle over Bluetooth and over USB.
There were 64 full and diagonal releases and 50 partial releases (let go from
35–85%) per stick and link. A bounce counts as left over when the stick leaves
the deadzone again after first reaching centre while it is not held.

| Stick | Hold | Left over past 3% | Left over past 5% |
|-------|------|-------------------|-------------------|
| Stock (left) | 25 ms | 0 of 228 | 0 of 228 |
| TMR (right) | 55 ms | 6 of 228 | 0 of 228 |
| TMR (right) | 60 ms | 1 of 228 | 0 of 228 |
| TMR (right) | 65 ms | 0 of 228 | 0 of 228 |

Partial releases were all caught, by both sticks at every hold above. The TMR
stick's bounce is slower to die out. Its last swing past 3% came as late as
64 ms after the release, against 25 ms for the stock stick.

A longer hold costs nothing while the stick is resting. It only delays a
deliberate move that starts within the hold and that none of the escape rules
below recognise. To check that, 123 deliberate moves across centre were
recorded over Bluetooth. Each started from about 40% to full and stopped 3–85%
past centre on the other side, with the thumb on the stick throughout. With a
65 ms hold, once the stick was more than 5% past centre the filter held it at
most 7.6 ms, and for 5 ms or less in most cases. Moves that stopped 10% or more
past centre were held at most 6.3 ms.

Slams from one edge to the other are the hardest case. 74 slams were recorded
over Bluetooth, driven by the thumb in every direction at full speed. The
fastest crossed from 35% to 15% in 3–4 ms, as fast as the spring's 1–8.5 ms,
so the release window cannot tell them apart and the filter starts a hold.
Counted from the stick passing 5% on the far side, they were held as follows.

| Stick | Hold | Not held | Median | Longest |
|-------|------|----------|--------|---------|
| Stock (left) | 25 ms | 10 of 33 | 5.0 ms | 12.3 ms |
| TMR (right) | 65 ms | 16 of 41 | 3.8 ms | 10.1 ms |

The steady push rule ends most of these holds. Right after centre, a slam and
the spring's overshoot both shoot outward fast, and only the slam keeps going.
Waiting 10 ms is about as short as that rule can be, since 8 ms already lets
modelled bounces through. A slam then reaches the far side up to about 10 ms
late, during a crossing that takes 15–25 ms anyway.

## How a release is detected

All thresholds are fractions of full travel, measured from `0x80`. They are in
`src/snapback_filter.h`.

1. **Armed.** The stick is at 35% or further. Its position and the furthest
   it reached are remembered. If it reaches the other side without passing
   centre, as during a report gap, the furthest point starts over, so it always
   belongs to the push the stick is let go from. The furthest point is also
   only the largest deflection of the last 15–30 ms, kept as the larger of two
   15 ms windows. A release reaches centre within 15 ms, so it is judged
   against the push it starts from, even when the stick sits a count or two
   below its first reading before it. A stick pushed all the way, eased back
   to 45% and let go from there is judged against 45%, not the full push. A
   gap of more than 6 ms does not age it. An earlier version kept only the
   time of the furthest reading and replaced it after 15 ms. On a stick
   jittering at full travel that time went stale, and the replacement was a
   reading taken mid-release, so 6 recorded TMR overshoots counted as hard
   pushes. Let go from 35–50%, the TMR stick still
   overshot by up to half that push, which is why the arm point is not higher.
2. **Released.** Within 15 ms of last being armed, the stick's position along
   the remembered direction comes back to 15% or less. It does not have to land
   near centre. The spring often carries it straight past centre between two
   reports. From a full push the spring covers this distance in 1–6 ms, and
   from a partial push usually within 10 ms, so a slower return is the player
   moving the stick and is not held. A gap of more than 6 ms
   between reports does not count toward the 15 ms. Over Bluetooth, reports
   sometimes stop for 15–25 ms, and when that happens during a release the
   stick comes back already bouncing. A recorded release like that got past
   the filter before this rule.

   A report gap can also hide the return entirely, so the next report shows
   the stick already 35% or more past centre on the other side. Straight after
   a push, that is the spring's overshoot, so it also counts as a release.
   Only a push past 80% of the one before, or one 35% or more to the side,
   counts as a new push there. A full flick from one side to the other passes,
   since it goes about as far as the push before.
3. **Holding.** The stick is reported at centre until one of these happens.
   - The hold time has passed since the release.
   - **Settled.** The stick has stayed within 3% of centre for 8 ms. A bounce
     swings through that zone faster. The zone suits the low deadzones (about
     0–5%) used with TMR sticks. A gap of more than 6 ms between reports
     starts the 8 ms over, since the stick may have swung out and back during
     it. A stick that rests further than 3% off centre, such as a worn
     potentiometer stick, never settles. If it rests at 5% or more, or jitters
     up to it, the steady push rule ends its hold. Otherwise it gets the full
     hold.
   - **Hard push.** The stick is past 80% of the push it was let go from. The
     first overshoot measured stays below that, at most 53% of the push on the
     stock stick and 76% on the TMR stick, so the margin on a TMR stick is
     small.
   - **Sideways.** The stick is 35% or more to the side of the line it was
     pushed along. Bounces stay on that line. Diagonal releases drifted up to
     16% to the side.
   - **Steady push.** The stick is at 5% or more and has not moved back
     towards centre for 10 ms. The 10 ms count from the last move back,
     including time spent below 5%. A bounce turns around within a few ms, while a
     thumb that moves the stick across centre stops and stays there. A gap of
     more than 6 ms between reports starts the 10 ms over, since the stick may
     have bounced during it. This rule used to start at 25%, so a deliberate
     move that stopped 10–25% past centre was held for most of the hold, up to
     50 ms in the recordings.
4. When the hold ends the stick passes through again, and a new push of 35% or
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

- Test in a game with a known deadzone, with both sticks, including how quick
  direction changes feel.
- Recordings from other sticks, to check the 80% hard push margin on TMR
  sticks that overshoot more than this one.
