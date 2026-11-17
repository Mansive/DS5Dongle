# Button layer (modifier remap)

Status: implemented in firmware, `tools/config_tool.py`, and the web UI
(`ds5dongle-config-web`, branch `feat/button-layer`). Host tests pass. The
firmware has **not** been built for the Pico or tested on hardware yet. See
[Open items](#open-items).

## What it does

Hold a modifier button (for example the DualSense Edge left paddle) and press
another button to emit a different controller input, such as paddle + face
button → D-pad direction. It works as a second remap table (a "layer") that
applies only to buttons pressed while the modifier is held. It is part of
Remap, not Shortcuts, because Shortcuts only emit keyboard and media keys.

## Behaviour (final)

- **Modifier.** Any ButtonId from Square to RightPaddle, or `0xFF` for off.
  D-pad directions cannot be the modifier. It is read from the **physical**
  state only, so mapping another button *to* it never activates the layer.
- **The modifier is consumed.** It is hidden from the game, and any base
  remap of it is ignored while it is the modifier. Pressing it alone does
  nothing; there is no tap-through.
- **Per-button latch.** A button pressed while the modifier is held keeps
  using the layer until that button is released, whatever the modifier does
  meanwhile. A button pressed *before* the modifier keeps its base mapping.
  The latch comes from "pressed now" versus "pressed on the last call". Running
  the remap many times on one BT report, which polling modes 0–2 do, therefore
  never changes the result.
- **Inheritance.** A layer entry left at identity inherits the base mapping
  (UI: "Same as base"). As a result, the layer cannot map a button back to
  itself when the base remaps it.
- **D-pad merging.** Every D-pad target, plus whatever remains of the physical
  D-pad, is combined as up/down/left/right bits. Two directions make a
  diagonal, and opposite directions cancel to neutral (SOCD neutral). This also
  applies to base remaps. *Behaviour change:* two base sources mapped to D-pad
  directions now combine instead of the last one winning.
- **Diagonal quirk kept.** A cardinal source such as "D-pad North → X" still
  matches only when the hat is exactly North, not NE or NW.
- **Triggers.** Only the digital L2/R2 bit is remapped. The analog axis is
  untouched, the same as for base remaps.
- **Resets.** Latches clear on BT disconnect and on every `0xFC` write.

## Protocol and storage

- New feature report **`0xFC`**, declared as 63 bytes in both the DS and DSE
  descriptors. Byte 0 is the modifier and bytes 1–28 are the layer table, using
  the same encoding as `0xFA`. GET returns the current layer. SET copies up to
  29 bytes, validates them, and resets the latches. Persist with `0xF6`
  funcid `0x02`.
- `0xFA` is unchanged. Extending it would let old web UIs, which zero-pad
  their 63-byte write, fill the layer with zeros ("everything → D-pad Up").
- Descriptor lengths: DS 337 → 345 and DSE 453 → 461. Both the static
  `wDescriptorLength` and the runtime patch (`0x59` / `0xCD`) are updated.
- Storage: `ButtonLayer` is appended to `Button`, with no `CONFIG_VERSION`
  bump. Older flash reads `0xFF` there, which validation turns into "layer
  off" with an identity table.
- Validation (`button_layer_valid`):
  - An invalid modifier becomes `0xFF`.
  - Entries above `Disable` become identity.
  - `remap[modifier]` is forced to identity.

## Tools

```sh
python tools/config_tool.py layer
python tools/config_tool.py layer modifier=LeftPaddle --face-dpad
python tools/config_tool.py layer modifier=LeftPaddle cross=down triangle=up
python tools/config_tool.py layer cross=nomap
python tools/config_tool.py layer modifier=off
python tools/config_tool.py layer --reset
```

`get` also prints the layer, and skips it on firmware without `0xFC`.

## Web UI

- The Remap page gets a **Base / Layer** toggle. It appears only when the
  device's HID descriptor declares report `0xFC`, so older firmware hides it.
- Layer mode adds:
  - a layer-button picker, with paddles first
  - a dashed outline on the modifier hotspot
  - "Same as base" labels on entries that inherit the base mapping
- Warnings cover:
  - Fn chosen as the modifier
  - a base remap on the modifier
  - a layer with no modifier
  - L2/R2 analog values not being remapped
- Save writes `0xFA` and/or `0xFC`, whichever changed, verifies each by
  reading it back, and then saves to flash.

## Edge cases and how they ended up

| # | Case | Result |
|---|------|--------|
| 1 | Paddle released before the face button | Latch keeps the layer output until the face button is released |
| 2 | Face button pressed before the paddle | Keeps its base mapping (predictable) |
| 3 | Remap applied repeatedly to one report | Idempotent; latch is derived from state |
| 4 | Stale state after disconnect | Latches reset on disconnect. **Open:** check what report `main.cpp` sends after a disconnect |
| 5 | Paddle pressed alone | Hidden; tap-through out of scope |
| 6 | Base table remaps the modifier | Ignored while it is the modifier; UI warns |
| 7 | Another button mapped *to* the modifier | Modifier is read from physical state only |
| 8 | Layer entry for the modifier itself | Forced to identity (firmware, tool, UI) |
| 9 | Common button as modifier | Allowed |
| 10 | Two directions at once | Combine into a diagonal |
| 11 | Opposite directions | Cancel to neutral |
| 12 | Layer direction plus physical D-pad | Merge |
| 13 | Diagonal quirk on cardinal sources | Kept as before |
| 14 | Swaps inside the layer | Two-phase clear-then-fill, as in base |
| 15 | L2/R2 | Digital bit only; UI hint in layer mode |
| 16 | Edge Fn combos | Fn allowed as modifier, with a warning |
| 17 | Edge onboard profile reassigns paddles | **Open:** hardware check |
| 18 | Regular DualSense | Paddle layer never activates; other modifiers offered |
| 19 | DS5 emulation with an Edge | Should work; **open:** hardware check |
| 20 | Overlap with Shortcuts | Not flagged in the UI. Both fire, because shortcuts read physical state |
| 21 | Mixed versions | Old UI leaves the layer alone; `config_tool.py get` shows it. New UI hides the layer on old firmware |
| 22 | Flash up/downgrade | `0xFF` reads as "layer off"; no version bump |

## Known limitations

- While the layer is enabled, the modifier is consumed in the output report.
  The web UI's Shortcuts "Listen to controller" capture therefore cannot see
  it. Type that trigger manually instead.
- The base remap's `resolvePhysicalButton` (used by capture) does not account
  for the layer.

## Tests

- `python tests/test_layer.py`: tool unit tests, plus a firmware test. The
  firmware test compiles the real `button_remap.cpp`, `button_utils.cpp`, the
  real report layout from `utils.h`, and the real `button_layer_valid` /
  `set_button_layer` from `config.cpp` with g++, then runs
  `tests/layer_remap_test.cpp`. It covers latch, release order, idempotence,
  D-pad merge and cancel, swaps, inheritance, modifier rules, resets, and
  validation.
- Web UI: `bun test` (`tests/button-layer.test.ts`: encode/decode,
  normalization, `0xFC` support detection) and `pnpm build`. The UI was also
  driven in a browser against a simulated device. It confirmed:
  - the exact `0xFC` bytes for the face-button preset
  - the save-to-flash command
  - that the toggle is hidden with no `0xFC` read on old firmware

## Open items

- Build the firmware (`tools/build-windows.ps1` installs the toolchain), flash
  it, and test on hardware:
  - Edge paddles over BT with the default profile
  - DS5 emulation mode
  - the report sent after a disconnect
- Optional: flag Shortcut triggers that overlap with layer mappings.
