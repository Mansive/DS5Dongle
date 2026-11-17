# Touchpad mouse and mouse-button remaps

Status: implemented in firmware, `tools/config_tool.py`, and the web UI
(`ds5dongle-config-web`, branch `feat/touchpad-mouse`). The firmware builds
and the host tests pass. It has **not** been tested on hardware yet. See
[Open items](#open-items).

## What it does

- **Touchpad mouse.** One finger on the controller touchpad moves the
  computer's mouse cursor.
- **Mouse-button remaps.** Any button can be remapped to a mouse button: left,
  right, middle, back (button 4) or forward (button 5). This works in the base
  remap and in every [button layer](button-layer.md).
- **Toggle shortcut.** A shortcut action pauses and resumes the touchpad mouse
  without changing the saved setting.

The two features are independent. Mouse-button remaps work with the touchpad
mouse off, and the touchpad mouse works without any mouse-button remap.
Two-finger scrolling is not implemented.

## Settings

| Setting | Values | Default |
|---------|--------|---------|
| `touchpad_mouse` | `0` off, `1` on (the game still sees the touchpad), `2` on, and touches are hidden from the game | `0` |
| `touchpad_sensitivity` | `1`–`20` | `8` |

- **Mode 1** suits desktop use, where nothing reads the touchpad anyway.
- **Mode 2** suits games that react to touches, so moving the cursor does not
  also swipe in the game. Only touch positions are hidden. The touchpad
  *click* (the Pad button) still reaches the game unless it is remapped.
- **Sensitivity.** At the default of 8, a slow stroke across the full width of
  the touchpad moves the cursor about 770 counts. There is mild acceleration:
  fast strokes move up to 2.5 times further than slow ones. Each step adds
  0.05 to the base gain.

## Behaviour

- Only the first finger moves the cursor. A second finger is ignored.
- Putting a finger down never moves the cursor. Movement starts with the next
  report. A new finger (a new touch index) starts over the same way.
- A jump of more than 400 touchpad units between two reports is treated as a
  glitch and ignored.
- Fractions of a count carry over between reports, so slow movement is not
  lost.
- Movement that cannot be sent while the USB endpoint is busy is kept and sent
  with the next report.
- Mouse buttons come from the same remap pass as the gamepad report, so layers
  and their per-button latch apply to them. A button mapped to a mouse button
  is removed from the gamepad report, like any other remap.
- Pressed mouse buttons are released on BT disconnect.
- The toggle shortcut affects only the running session. It does nothing while
  `touchpad_mouse` is `0`, and it resets to "on" after a reboot. While paused,
  touches reach the game even in mode 2. Mouse-button remaps keep working.

## USB

- Mouse reports are **Report ID 3** on the consumer-control HID interface
  (interface instance 2, endpoint `0x88`), next to the media keys (ID 1) and
  system control (ID 2). There is no extra USB interface.
- Report layout: 5 button bits plus 3 padding bits, then X and Y as signed
  16-bit relative values (logical range −32767 to 32767). The report is 5
  bytes after the ID.
- That interface now polls every **1 ms** instead of 10 ms, so the cursor
  moves smoothly.
- The keyboard and consumer interfaces are only enumerated when something
  needs them: `enable_wake`, `enable_keyboard`, `sleep_on_dock`, a non-zero
  `touchpad_mouse`, or a remap (base or layer) that targets a mouse button.
- **Reconnect USB** (or replug) after turning the touchpad mouse on or off,
  and after saving the first mouse-button remap. The interface is only added
  or removed on re-enumeration. The web UI shows a reminder for both cases.

## Protocol and storage

- `Config_body` grows from 25 to 27 bytes. `touchpad_mouse` is at offset 25
  and `touchpad_sensitivity` at offset 26. `CONFIG_VERSION` stays 5.
- Older web UIs send a padded 63-byte `0xF6` report, so both new bytes arrive
  as 0. The firmware reads 0 as "off" and "default sensitivity", so an older
  UI never turns the feature on by accident.
- Clients detect support from the `0xF7` read-back. Firmware with the feature
  always reports a sensitivity from 1 to 20. Older firmware returns 0 or stops
  before that byte.
- Remap targets: `Disable` stays 27. `MouseLeft` = 28, `MouseRight` = 29,
  `MouseMiddle` = 30, `MouseBack` = 31, `MouseForward` = 32. These are valid
  targets in `0xFA`, `0xFC` and `0xFD`, but never sources, hold buttons or
  shortcut triggers. Older firmware turns them into identity mappings when it
  validates the table.
- Shortcut action `3` is the touchpad mouse toggle. It takes no payload and
  cannot use the hold gestures.

## Tools

```sh
python tools/config_tool.py set touchpad_mouse=1 touchpad_sensitivity=10
python tools/config_tool.py set touchpad_mouse=2       # hide touches from the game
python tools/config_tool.py remap r1=mouseleft l1=mouseright
python tools/config_tool.py layer --layer 1 hold=LeftPaddle cross=mouseback
python tools/config_tool.py shortcut 7=Pad*2:toggle_touchpad_mouse
```

- Mouse target names: `mouseleft`, `mouseright`, `mousemiddle`, `mouseback`,
  `mouseforward`. The aliases `leftclick`, `rightclick`, `middleclick`,
  `mouse4` and `mouse5` also work.
- The shortcut action is `toggle_touchpad_mouse` (or `touchpadmouse`).

## Web UI

- **General** page: a **Touchpad mouse** section with the mode and the
  sensitivity. It only appears on firmware that supports the feature.
  Sensitivity is greyed out while the mode is off.
- **Remap** page: the output dropdown gains a **Mouse** group with the five
  mouse buttons, for the base table and every layer. Choosing one while the
  mouse interface is not enumerated shows the reconnect reminder.
- **Shortcuts** page: a **Toggle touchpad mouse** output action.
- Saving the General settings now writes all 27 bytes and keeps
  `sleep_on_dock` as read. Before this change, a web save reset
  `sleep_on_dock` to off.

## Tests

- `python tests/test_touchpad_mouse.py`: tool tests, plus a firmware test that
  compiles the real `touchpad_mouse.cpp` and `button_remap.cpp` with g++ and
  runs `tests/touchpad_mouse_test.cpp`. It covers touch-down, finger changes,
  jumps, sensitivity and acceleration, remainder carry, hiding touches, mouse
  targets in base and layer tables, layer-table validation, the config byte
  layout, and the report descriptor size.
- `python tests/test_layer.py` and `python tests/test_hold.py` still pass.
- Web UI: `bun test` (`tests/touchpad-mouse.test.ts`: the 27-byte config
  layout, support detection with older firmware, mouse targets in `0xFA` and
  layers, the toggle action) and `bun run build`. The UI was also driven in a
  browser against a simulated device. That check confirmed:
  - the General section appears, and the mode is written to byte 25
  - the reconnect notice after turning the touchpad mouse on
  - a mouse target saved through `0xFA` and read back
  - the toggle action saved as action 3

## Open items

- Flash the firmware and test on hardware:
  - cursor feel at the default sensitivity, and the acceleration curve
  - mode 2 in a game that uses the touchpad
  - mouse buttons over BT, including in layers, and their release on disconnect
  - Windows, macOS and Linux enumeration of the extra report ID
  - the 1 ms interval with media keys and wake still working
- Possible later additions: two-finger scrolling, tap to click.
