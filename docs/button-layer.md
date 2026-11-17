# Button layers (modifier remap)

Status: implemented in firmware, `tools/config_tool.py`, and the web UI
(`ds5dongle-config-web`, branch `feat/button-layer`). The firmware builds and
the host tests pass. It has **not** been tested on hardware yet. See
[Open items](#open-items).

## What it does

Layer buttons work like keyboard modifiers (Shift, Alt, Shift+Alt). There are
two layer buttons, **A** and **B**, for example the two DualSense Edge back
paddles. Holding them selects one of three extra remap tables:

| Held | Table |
|------|-------|
| nothing | base remap (`0xFA`) |
| A only | layer A |
| B only | layer B |
| A and B | layer A + B: its own table, **not** a merge of A and B |

A typical use is paddle + face button → D-pad direction. Layers are part of
Remap, not Shortcuts, because Shortcuts only emit keyboard and media keys.

## Behaviour

- **Layer buttons.** Each is any ButtonId from Square to RightPaddle, or
  `0xFF` for off. D-pad directions cannot be layer buttons. They are read from
  the **physical** state only, so mapping another button *to* one never
  activates a layer. B cannot equal A: when a write makes them equal, B is
  turned off (the newest write wins, and `0xFC` is written before `0xFD`).
- **Layer buttons are consumed.** They are hidden from the game, and any base
  remap of them is ignored while they are layer buttons. Pressing one alone
  does nothing; there is no tap-through.
- **Exact match.** With both held, only the A + B table is used. If A + B is
  left at identity, the button falls back to the **base** mapping, not to
  layer A or B. The A + B table needs both layer buttons set; otherwise it can
  never apply.
- **Per-button latch.** When a button is pressed, the firmware stores which
  layer buttons were held at that moment (`BUTTON_LAYER_HELD_A` = 1,
  `BUTTON_LAYER_HELD_B` = 2). The button keeps using that table until it is
  released, whatever the layer buttons do meanwhile. A button pressed *before*
  any layer button keeps its base mapping. The latch comes from "pressed now"
  versus "pressed on the last call", so running the remap many times on one BT
  report, which polling modes 0–2 do, never changes the result.
- **Inheritance.** A layer entry left at identity inherits the base mapping
  (UI: "Same as base"). As a result, a layer cannot map a button back to
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
- **Resets.** Latches clear on BT disconnect and on every `0xFC` or `0xFD`
  write.

## Protocol and storage

- Feature report **`0xFC`** (unchanged, 29 bytes): byte 0 is layer button A,
  bytes 1–28 are table A, using the same encoding as `0xFA`.
- Feature report **`0xFD`** (new, 57 bytes): byte 0 is layer button B,
  bytes 1–28 are table B, bytes 29–56 are table A + B.
- Both are declared as 63 bytes in the DS and DSE descriptors. GET returns the
  current values. SET copies up to the payload size, validates it, and resets
  the latches. Persist with `0xF6` funcid `0x02`.
- Clients write `0xFC` first, then `0xFD`.
- `0xFA` is unchanged. Extending it would let old web UIs, which zero-pad
  their 63-byte write, fill the layer with zeros ("everything → D-pad Up").
- Descriptor lengths: DS 353 (`0x161`) and DSE 469 (`0x1D5`). Both the static
  `wDescriptorLength` and the runtime patch (`0x61` / `0xD5`) are updated.
- Storage: `ButtonLayer` (29 bytes) and `ButtonLayer2` (57 bytes) are appended
  to `Button`, with no `CONFIG_VERSION` bump. `ConfigStorage` is 241 of the
  256 bytes allowed. Older flash reads `0xFF` there, which validation turns
  into "layer off" with identity tables.
- Validation (`button_layer_valid`):
  - An invalid layer button becomes `0xFF`.
  - B becomes `0xFF` when it equals A.
  - Entries above `Disable` become identity in all three tables.
  - Both layer buttons are forced to identity in all three tables.

## Tools

```sh
python tools/config_tool.py layer                                   # show all layers
python tools/config_tool.py layer buttons=LeftPaddle,RightPaddle    # set A and B
python tools/config_tool.py layer a=LeftPaddle b=off                # set them one at a time
python tools/config_tool.py layer --layer a --face-dpad
python tools/config_tool.py layer --layer a cross=down triangle=up
python tools/config_tool.py layer --layer b cross=l1
python tools/config_tool.py layer --layer ab cross=r3 circle=disable
python tools/config_tool.py layer --layer ab cross=nomap
python tools/config_tool.py layer --reset                           # everything off
```

- `--layer` picks the table that `source=target` pairs edit: `a` (default),
  `b` or `ab`.
- `modifier=` is kept as an alias for `a=`.
- On firmware without `0xFD`, only layer A is read and written.
- `get` also prints the layers, and skips them on firmware without `0xFC`.

## Web UI

- The Remap page has **Base / Layer A / Layer B / A + B** tabs. On firmware
  that declares only `0xFC`, there is a single **Layer** tab. With neither
  report, the tabs are hidden.
- The Layer A tab shows the layer button A picker, Layer B shows B, and A + B
  shows both. Each picker leaves out the button chosen for the other.
- Layer tabs add:
  - a dashed outline on the layer button hotspots
  - "Same as base" labels on entries that inherit the base mapping
  - a per-table **Reset layer** button that clears only that table
- Warnings cover:
  - Fn chosen as a layer button
  - a base remap on a layer button
  - a layer with no layer button, or the A + B layer missing one of them
  - L2/R2 analog values not being remapped
- Save writes `0xFA` and/or the layers (`0xFC`, then `0xFD`), verifies each by
  reading it back, and then saves to flash.

## Edge cases and how they ended up

| # | Case | Result |
|---|------|--------|
| 1 | Layer button released before the face button | Latch keeps the layer output until the face button is released |
| 2 | Face button pressed before a layer button | Keeps its base mapping (predictable) |
| 3 | A held, then B pressed, then Cross | Cross uses A + B |
| 4 | Cross pressed with A, then B pressed | Cross stays on layer A until released |
| 5 | A + B entry left unmapped | Falls back to base, not to A or B |
| 6 | Remap applied repeatedly to one report | Idempotent; latch is derived from state |
| 7 | Stale state after disconnect | Latches reset on disconnect. **Open:** check what report `main.cpp` sends after a disconnect |
| 8 | Layer button pressed alone | Hidden; tap-through out of scope |
| 9 | Base table remaps a layer button | Ignored while it is a layer button; UI warns |
| 10 | Another button mapped *to* a layer button | Layer buttons are read from physical state only |
| 11 | Layer entry for a layer button | Forced to identity in all tables (firmware, tool, UI) |
| 12 | B set to the same button as A | B turned off |
| 13 | Common button as a layer button | Allowed |
| 14 | Two directions at once | Combine into a diagonal |
| 15 | Opposite directions | Cancel to neutral |
| 16 | Layer direction plus physical D-pad | Merge |
| 17 | Swaps inside a layer | Two-phase clear-then-fill, as in base |
| 18 | L2/R2 | Digital bit only; UI hint in layer tabs |
| 19 | Edge Fn combos | Fn allowed as a layer button, with a warning |
| 20 | Edge onboard profile reassigns paddles | **Open:** hardware check |
| 21 | Regular DualSense | Paddles not offered; other buttons can be layer buttons |
| 22 | DS5 emulation with an Edge | Should work; **open:** hardware check |
| 23 | Overlap with Shortcuts | Not flagged in the UI. Both fire, because shortcuts read physical state |
| 24 | Mixed versions | Old UI leaves `0xFD` alone. New UI on `0xFC`-only firmware shows one Layer tab |
| 25 | Flash up/downgrade | `0xFF` reads as "layer off"; no version bump |

## Known limitations

- Enabled layer buttons are consumed in the output report. The web UI's
  Shortcuts "Listen to controller" capture therefore cannot see them. Type that
  trigger manually instead.
- The base remap's `resolvePhysicalButton` (used by capture) does not account
  for layers.

## Tests

- `python tests/test_layer.py`: tool unit tests, plus a firmware test. The
  firmware test compiles the real `button_remap.cpp`, `button_utils.cpp`, the
  real report layout from `utils.h`, and the real `button_layer_valid` /
  `set_button_layer` / `set_button_layer2` from `config.cpp` with g++, then
  runs `tests/layer_remap_test.cpp`. It covers latch, release order,
  idempotence, the A + B table and its latching, layer B alone, D-pad merge
  and cancel, swaps, inheritance, layer button rules, resets, and validation.
- Web UI: `bun test` (`tests/button-layer.test.ts`: encode/decode for `0xFC`
  and `0xFD`, normalization, support detection, `0xFC`-then-`0xFD` write
  order) and `bun run build`. The UI was also driven in a browser against a
  simulated device. That check confirmed:
  - the four tabs
  - each picker leaving out the other layer button
  - a save writing `0xFC`, then `0xFD` with the A + B entry, then the
    save-to-flash command

## Open items

- Flash the firmware and test on hardware:
  - Edge paddles over BT with the default profile, alone and together
  - DS5 emulation mode
  - the report sent after a disconnect
- Optional: flag Shortcut triggers that overlap with layer mappings.
