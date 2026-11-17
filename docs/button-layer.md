# Button layers (modifier remap)

Status: implemented in firmware, `tools/config_tool.py`, and the web UI
(`ds5dongle-config-web`, branch `feat/button-layer`). The firmware builds and
the host tests pass. It has **not** been tested on hardware yet. See
[Open items](#open-items).

## What it does

Layer buttons work like keyboard modifiers (Shift, Alt, Shift+Alt). There are
three layers, each with its own remap table and its own **set of hold
buttons**: one button or any combination. A layer applies while the held
layer buttons are **exactly** its set. With the DualSense Edge paddles:

| Layer | Hold set | Applies while held |
|-------|----------|--------------------|
| — | — | nothing: base remap (`0xFA`) |
| 1 | LeftPaddle | left paddle only |
| 2 | RightPaddle | right paddle only |
| 3 | LeftPaddle + RightPaddle | both paddles: its own table, **not** a merge of 1 and 2 |

Any layout works, for example a single layer on L1 + R1, or three layers on
three different buttons. A typical use is paddle + face button → D-pad
direction. Layers are part of Remap, not Shortcuts, because Shortcuts only
emit keyboard and media keys.

## Behaviour

- **Hold buttons.** Any ButtonId from Square to RightPaddle. D-pad directions
  cannot be hold buttons. An empty set turns the layer off. Hold buttons are
  read from the **physical** state only, so mapping another button *to* one
  never activates a layer.
- **Layer buttons.** Every button in any layer's hold set is a layer button.
- **Layer buttons are consumed.** They are hidden from the game all the time,
  and any base remap of them is ignored. Pressing one alone does nothing;
  there is no tap-through. Using a common button such as L1 in a hold set
  therefore removes L1 from the game entirely.
- **Exact match.** The firmware takes the physically held layer buttons and
  looks for the layer whose hold set equals them. If none matches (e.g. layer
  3 is L1 + R1 and only L1 is held, with no layer on L1 alone), buttons use
  the base mapping. An entry left at identity falls back to the **base**
  mapping, never to another layer.
- **No duplicates.** Two layers cannot have the same hold set. The firmware
  keeps the set on the earlier layer and turns the later one off; the tool
  and the web UI refuse such a set.
- **Per-button latch.** When a button is pressed, the firmware stores which
  layer was active at that moment (1–3, or 0 for base). The button keeps using
  that table until it is released, whatever the layer buttons do meanwhile. A
  button pressed *before* any layer button keeps its base mapping. The latch
  comes from "pressed now" versus "pressed on the last call", so running the
  remap many times on one BT report, which polling modes 0–2 do, never changes
  the result.
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

- Each layer is 31 bytes:
  - bytes 0–2: hold bitmask, little endian. Bit *n* is ButtonId Square + *n*
    (bit 0 = Square, bit 18 = RightPaddle). Higher bits are cleared.
  - bytes 3–30: the 28-entry table, using the same encoding as `0xFA`.
- Feature report **`0xFC`** (62 bytes): layer 1, then layer 2.
- Feature report **`0xFD`** (31 bytes): layer 3.
- Both are declared as 63 bytes in the DS and DSE descriptors. GET returns the
  current values. SET copies up to the payload size, validates all layers, and
  resets the latches. Persist with `0xF6` funcid `0x02`.
- Clients write `0xFC` first, then `0xFD`.
- `0xFA` is unchanged. Extending it would let old web UIs, which zero-pad
  their 63-byte write, fill the layer with zeros ("everything → D-pad Up").
- Storage: `ButtonLayers` (a format byte plus 3 × 31 = 94 bytes) is appended
  to `Button`, with no `CONFIG_VERSION` bump. `ConfigStorage` is 249 of the
  256 bytes allowed.
- Format byte `BUTTON_LAYER_FORMAT` (`0x4C`). Any other value (`0xFF` from
  older firmware, or an earlier layer layout) resets every layer to off with
  identity tables.
- Validation (`button_layer_valid`):
  - Hold bits past RightPaddle are cleared.
  - A hold set equal to an earlier layer's is cleared.
  - Entries above `Disable` become identity in every table.
  - Every layer button is forced to identity in every table.

## Tools

```sh
python tools/config_tool.py layer                                           # show all layers
python tools/config_tool.py layer --layer 1 hold=LeftPaddle --face-dpad
python tools/config_tool.py layer --layer 2 hold=RightPaddle cross=l1 circle=r1
python tools/config_tool.py layer --layer 3 hold=LeftPaddle,RightPaddle cross=r3
python tools/config_tool.py layer --layer 3 hold=L1+R1                      # + also separates buttons
python tools/config_tool.py layer --layer 2 cross=nomap
python tools/config_tool.py layer --layer 2 hold=off                        # turn off, keep the table
python tools/config_tool.py layer --reset                                   # everything off
```

- `--layer` (1, 2 or 3; default 1) picks the layer that `hold=` and
  `source=target` edit.
- The tool rejects a hold set used by another layer and remapping any layer
  button.
- `get` also prints the layers, and skips them on firmware without both
  `0xFC` and `0xFD`.

## Web UI

- The Remap page has **Base / Layer 1 / Layer 2 / Layer 3** tabs, shown when
  the firmware declares both `0xFC` and `0xFD`.
- Each layer tab has one **Hold button N** dropdown per hold button, plus an
  empty one (**None**) to add another; choosing None removes a button. Paddles
  and Fn are offered in the DualSense Edge view; a hold button not offered in
  the current view is still shown. A choice that would give this layer another
  layer's hold set is disabled and labelled "(used by layer N)".
- Layer tabs add:
  - a dashed outline on this layer's hold button hotspots
  - "Same as base" labels on entries that inherit the base mapping
  - a per-layer **Reset layer** button that clears only that table
- Warnings cover:
  - Fn among the hold buttons
  - a base remap on a hold button
  - mappings in a layer with no hold buttons
  - L2/R2 analog values not being remapped
- Save writes `0xFA` and/or the layers (`0xFC`, then `0xFD`), verifies each by
  reading it back, and then saves to flash.

## Edge cases and how they ended up

| # | Case | Result |
|---|------|--------|
| 1 | Hold button released before the face button | Latch keeps the layer output until the face button is released |
| 2 | Face button pressed before a hold button | Keeps its base mapping (predictable) |
| 3 | Left paddle held, then right paddle, then Cross | Cross uses the layer whose set is both paddles |
| 4 | Cross pressed in layer 1, then the second paddle pressed | Cross stays on layer 1 until released |
| 5 | Held layer buttons match no layer | Base mapping |
| 6 | Layer entry left unmapped | Falls back to base, not to another layer |
| 7 | Remap applied repeatedly to one report | Idempotent; latch is derived from state |
| 8 | Stale state after disconnect | Latches reset on disconnect. **Open:** check what report `main.cpp` sends after a disconnect |
| 9 | Layer button pressed alone | Hidden; tap-through out of scope |
| 10 | Base table remaps a layer button | Ignored; UI warns |
| 11 | Another button mapped *to* a layer button | Layer buttons are read from physical state only |
| 12 | Layer entry for a layer button | Forced to identity in all tables (firmware, tool, UI) |
| 13 | Two layers with the same hold set | Firmware clears the later one; tool and UI refuse it |
| 14 | Common button (e.g. L1) as a hold button | Allowed, but it is hidden from the game everywhere |
| 15 | Two directions at once | Combine into a diagonal |
| 16 | Opposite directions | Cancel to neutral |
| 17 | Layer direction plus physical D-pad | Merge |
| 18 | Swaps inside a layer | Two-phase clear-then-fill, as in base |
| 19 | L2/R2 | Digital bit only; UI hint in layer tabs |
| 20 | Edge Fn combos | Fn allowed as a hold button, with a warning |
| 21 | Edge onboard profile reassigns paddles | **Open:** hardware check |
| 22 | Regular DualSense | Paddles not offered; other buttons can be hold buttons |
| 23 | DS5 emulation with an Edge | Should work; **open:** hardware check |
| 24 | Overlap with Shortcuts | Not flagged in the UI. Both fire, because shortcuts read physical state |
| 25 | Flash up/downgrade | Unknown format byte reads as "all layers off"; no version bump |

## Known limitations

- Layer buttons are consumed in the output report. The web UI's Shortcuts
  "Listen to controller" capture therefore cannot see them. Type that trigger
  manually instead.
- The base remap's `resolvePhysicalButton` (used by capture) does not account
  for layers.

## Tests

- `python tests/test_layer.py`: tool unit tests, plus a firmware test. The
  firmware test compiles the real `button_remap.cpp`, `button_utils.cpp`, the
  real report layout from `utils.h`, and the real `button_layer_valid` /
  `set_button_layers` from `config.cpp` with g++, then runs
  `tests/layer_remap_test.cpp`. It covers latch, release order, idempotence,
  combination layers and their latching, a chord-only layer, a three-button
  layer, layer 2 alone, D-pad merge and cancel, swaps, inheritance, layer
  button rules, resets, and validation.
- Web UI: `bun test` (`tests/button-layer.test.ts`: hold bitmask encoding,
  encode/decode for `0xFC` and `0xFD`, normalization, support detection,
  `0xFC`-then-`0xFD` write order) and `bun run build`. The UI was also driven
  in a browser against a simulated device. That check confirmed:
  - the Base / Layer 1 / Layer 2 / Layer 3 tabs
  - a dropdown choice that would duplicate another layer's set being disabled
  - a save writing `0xFC`, then `0xFD`, then the save-to-flash command, with
    the expected hold bitmasks

## Open items

- Flash the firmware and test on hardware:
  - Edge paddles over BT with the default profile, alone and together
  - DS5 emulation mode
  - the report sent after a disconnect
- Optional: flag Shortcut triggers that overlap with layer mappings.
