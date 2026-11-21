// Host test for src/button_remap.cpp (button layers). Built by tests/test_layer.py
// against the real USBGetStateData layout; see that file for the stubs.
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "button_remap.h"
#include "button_utils.h"
#include "config.h"
#include "utils.h"

void set_button_layers(uint8_t first_layer, const uint8_t *data, uint16_t len);

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void set_pressed(USBGetStateData &s, uint8_t b) {
    switch (b) {
        case ButtonSquare: s.ButtonSquare = 1; break;
        case ButtonCross: s.ButtonCross = 1; break;
        case ButtonCircle: s.ButtonCircle = 1; break;
        case ButtonTriangle: s.ButtonTriangle = 1; break;
        case ButtonL1: s.ButtonL1 = 1; break;
        case ButtonR1: s.ButtonR1 = 1; break;
        case ButtonL2: s.ButtonL2 = 1; break;
        case ButtonR2: s.ButtonR2 = 1; break;
        case ButtonCreate: s.ButtonCreate = 1; break;
        case ButtonOptions: s.ButtonOptions = 1; break;
        case ButtonL3: s.ButtonL3 = 1; break;
        case ButtonR3: s.ButtonR3 = 1; break;
        case ButtonHome: s.ButtonHome = 1; break;
        case ButtonPad: s.ButtonPad = 1; break;
        case ButtonMute: s.ButtonMute = 1; break;
        case ButtonLeftFunction: s.ButtonLeftFunction = 1; break;
        case ButtonRightFunction: s.ButtonRightFunction = 1; break;
        case ButtonLeftPaddle: s.ButtonLeftPaddle = 1; break;
        case ButtonRightPaddle: s.ButtonRightPaddle = 1; break;
        default: break;
    }
}

static USBGetStateData make(std::initializer_list<uint8_t> held, Direction dpad = None) {
    USBGetStateData s;
    std::memset(&s, 0, sizeof(s));
    // Non-button fields must pass through untouched.
    s.LeftStickX = 0x11;
    s.RightStickY = 0x22;
    s.TriggerLeft = 0x33;
    s.UNK2 = 0x44;
    s.UNK1 = 1;
    s.DPad = dpad;
    for (const uint8_t b : held) set_pressed(s, b);
    return s;
}

// Physical-button bitmask of an output report (D-pad excluded).
static uint32_t buttons(const USBGetStateData &s) {
    uint32_t mask = 0;
    for (uint8_t b = ButtonSquare; b < Disable; b++) {
        if (button_is_pressed(s, b)) mask |= 1u << b;
    }
    return mask;
}

static uint32_t mask(std::initializer_list<uint8_t> held) {
    uint32_t m = 0;
    for (const uint8_t b : held) m |= 1u << b;
    return m;
}

// Apply the remap `repeat` times to the same input (polling modes 0-2 resend
// one BT report many times) and require every pass to agree.
static USBGetStateData frame(std::initializer_list<uint8_t> held, Direction dpad = None, int repeat = 20) {
    const USBGetStateData in = make(held, dpad);
    USBGetStateData first = in;
    button_remap_apply(first);
    for (int i = 1; i < repeat; i++) {
        USBGetStateData again = in;
        button_remap_apply(again);
        CHECK(std::memcmp(&again, &first, sizeof(first)) == 0);
    }
    // Bytes outside the button area (7..9) are never modified.
    const auto *a = reinterpret_cast<const uint8_t *>(&in);
    const auto *b = reinterpret_cast<const uint8_t *>(&first);
    for (size_t i = 0; i < sizeof(in); i++) {
        if (i >= 7 && i <= 9) continue;
        CHECK(a[i] == b[i]);
    }
    CHECK(first.UNK1 == in.UNK1);
    return first;
}

static ButtonLayer &layer(uint8_t n) { return get_button().layers.layer[n]; }

// Set the hold buttons of layer n (0-based).
static void set_hold(uint8_t n, std::initializer_list<uint8_t> hold) {
    uint32_t bits = 0;
    for (const uint8_t b : hold) bits |= 1u << (b - ButtonSquare);
    layer(n).hold[0] = bits & 0xFF;
    layer(n).hold[1] = (bits >> 8) & 0xFF;
    layer(n).hold[2] = (bits >> 16) & 0xFF;
}

static void reset_config() {
    Button &button = get_button();
    button.layers.format = BUTTON_LAYER_FORMAT;
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) {
        button.button_remap[i] = i;
        for (auto &l : button.layers.layer) l.remap[i] = i;
    }
    for (uint8_t n = 0; n < BUTTON_LAYER_COUNT; n++) set_hold(n, {});
    button_remap_reset();
}

// Layer 1 = left paddle (face buttons -> D-pad), layer 2 = right paddle
// (Cross -> L1), layer 3 = both paddles (Cross -> R3, Circle -> Disable).
static void combo_layers() {
    set_hold(0, {ButtonLeftPaddle});
    layer(0).remap[ButtonTriangle] = DPadNorth;
    layer(0).remap[ButtonCross] = DPadSouth;
    layer(0).remap[ButtonSquare] = DPadWest;
    layer(0).remap[ButtonCircle] = DPadEast;
    set_hold(1, {ButtonRightPaddle});
    layer(1).remap[ButtonCross] = ButtonL1;
    set_hold(2, {ButtonLeftPaddle, ButtonRightPaddle});
    layer(2).remap[ButtonCross] = ButtonR3;
    layer(2).remap[ButtonCircle] = Disable;
}

static void face_dpad_layer(uint8_t modifier = ButtonLeftPaddle) {
    set_hold(0, {modifier});
    layer(0).remap[ButtonTriangle] = DPadNorth;
    layer(0).remap[ButtonCross] = DPadSouth;
    layer(0).remap[ButtonSquare] = DPadWest;
    layer(0).remap[ButtonCircle] = DPadEast;
}

static void test_identity_is_untouched() {
    reset_config();
    const USBGetStateData inputs[] = {
        make({}), make({ButtonCross, ButtonL1}), make({ButtonLeftPaddle}, NorthEast),
        make({ButtonRightPaddle, ButtonHome}, West),
    };
    for (const auto &in : inputs) {
        USBGetStateData out = in;
        button_remap_apply(out);
        CHECK(std::memcmp(&in, &out, sizeof(in)) == 0);
    }
}

static void test_base_remap_unchanged() {
    reset_config();
    Button &button = get_button();
    button.button_remap[ButtonSquare] = ButtonCross;
    button.button_remap[ButtonL1] = Disable;
    button.button_remap[ButtonR1] = DPadSouth;
    button.button_remap[DPadNorth] = ButtonTriangle;

    auto out = frame({ButtonSquare});
    CHECK(buttons(out) == mask({ButtonCross}));
    out = frame({ButtonL1});
    CHECK(buttons(out) == 0);
    out = frame({ButtonR1});
    CHECK(buttons(out) == 0 && out.DPad == South);
    out = frame({}, North);
    CHECK(buttons(out) == mask({ButtonTriangle}) && out.DPad == None);
    // Existing quirk kept: a remapped cardinal only matches the exact hat value.
    out = frame({}, NorthEast);
    CHECK(buttons(out) == 0 && out.DPad == NorthEast);
}

static void test_layer_basic() {
    reset_config();
    face_dpad_layer();

    auto out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == 0 && out.DPad == South);
    out = frame({});
    CHECK(buttons(out) == 0 && out.DPad == None);
    // Paddle alone is consumed.
    out = frame({ButtonLeftPaddle});
    CHECK(buttons(out) == 0 && out.DPad == None);
    out = frame({});
    // Without the paddle the face buttons are themselves.
    out = frame({ButtonCross});
    CHECK(buttons(out) == mask({ButtonCross}) && out.DPad == None);
    out = frame({});
    // Buttons with no layer entry keep working while the paddle is held.
    out = frame({ButtonLeftPaddle, ButtonR1, ButtonRightPaddle});
    CHECK(buttons(out) == mask({ButtonR1, ButtonRightPaddle}));
}

static void test_release_order() {
    reset_config();
    face_dpad_layer();

    // Paddle released before the face button: Down stays held.
    auto out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(out.DPad == South && buttons(out) == 0);
    out = frame({ButtonCross});
    CHECK(out.DPad == South && buttons(out) == 0);
    out = frame({});
    CHECK(out.DPad == None && buttons(out) == 0);
    out = frame({ButtonCross});
    CHECK(out.DPad == None && buttons(out) == mask({ButtonCross}));

    // Face button pressed before the paddle: stays Cross until released.
    out = frame({ButtonCross, ButtonLeftPaddle});
    CHECK(out.DPad == None && buttons(out) == mask({ButtonCross}));
    out = frame({ButtonLeftPaddle});
    CHECK(out.DPad == None && buttons(out) == 0);
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(out.DPad == South && buttons(out) == 0);

    // Pressed in the same report as the paddle: layer.
    out = frame({});
    out = frame({ButtonLeftPaddle, ButtonSquare});
    CHECK(out.DPad == West && buttons(out) == 0);
}

static void test_dpad_combining() {
    reset_config();
    face_dpad_layer();

    auto out = frame({ButtonLeftPaddle, ButtonTriangle, ButtonCircle});
    CHECK(out.DPad == NorthEast && buttons(out) == 0);
    out = frame({});
    out = frame({ButtonLeftPaddle, ButtonCross, ButtonSquare});
    CHECK(out.DPad == SouthWest);
    out = frame({});
    // Opposites cancel.
    out = frame({ButtonLeftPaddle, ButtonTriangle, ButtonCross});
    CHECK(out.DPad == None && buttons(out) == 0);
    out = frame({ButtonLeftPaddle, ButtonTriangle, ButtonCross, ButtonCircle});
    CHECK(out.DPad == East);
    out = frame({});
    // Layer direction merges with the physical D-pad.
    out = frame({ButtonLeftPaddle, ButtonCross}, East);
    CHECK(out.DPad == SouthEast);
    out = frame({ButtonLeftPaddle, ButtonCross}, North);
    CHECK(out.DPad == None);
    out = frame({});
    out = frame({ButtonLeftPaddle}, West);
    CHECK(out.DPad == West);
}

static void test_swap_and_disable_in_layer() {
    reset_config();
    Button &button = get_button();
    set_hold(0, {ButtonLeftPaddle});
    button.layers.layer[0].remap[ButtonCross] = DPadSouth;
    button.layers.layer[0].remap[ButtonSquare] = ButtonCross;
    button.layers.layer[0].remap[ButtonL1] = Disable;

    auto out = frame({ButtonLeftPaddle, ButtonSquare, ButtonCross, ButtonL1});
    CHECK(out.DPad == South && buttons(out) == mask({ButtonCross}));
}

static void test_layer_inherits_base() {
    reset_config();
    Button &button = get_button();
    button.button_remap[ButtonSquare] = ButtonTriangle;
    face_dpad_layer();
    button.layers.layer[0].remap[ButtonSquare] = ButtonSquare; // no layer entry

    auto out = frame({ButtonLeftPaddle, ButtonSquare});
    CHECK(buttons(out) == mask({ButtonTriangle}) && out.DPad == None);
}

static void test_modifier_rules() {
    // Base remap of the modifier is ignored while it is the modifier.
    reset_config();
    Button &button = get_button();
    button.button_remap[ButtonLeftPaddle] = ButtonL3;
    auto out = frame({ButtonLeftPaddle});
    CHECK(buttons(out) == mask({ButtonL3}));
    face_dpad_layer();
    out = frame({});
    out = frame({ButtonLeftPaddle});
    CHECK(buttons(out) == 0);

    // Mapping another button *to* the modifier does not activate the layer.
    reset_config();
    button.button_remap[ButtonR3] = ButtonLeftPaddle;
    face_dpad_layer();
    out = frame({ButtonR3, ButtonCross});
    CHECK(buttons(out) == mask({ButtonLeftPaddle, ButtonCross}) && out.DPad == None);

    // Another modifier choice.
    reset_config();
    face_dpad_layer(ButtonRightPaddle);
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonLeftPaddle, ButtonCross}));
    out = frame({});
    out = frame({ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == 0 && out.DPad == South);
}

static void test_resets() {
    // Disconnect: latch forgotten, a reconnect with Cross held is plain Cross.
    reset_config();
    face_dpad_layer();
    auto out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(out.DPad == South);
    button_remap_reset();
    out = frame({ButtonCross});
    CHECK(out.DPad == None && buttons(out) == mask({ButtonCross}));

    // Writing a new layer config clears the latch.
    reset_config();
    face_dpad_layer();
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(out.DPad == South);
    uint8_t disabled[sizeof(ButtonLayer)] = {0, 0, 0};
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) disabled[BUTTON_LAYER_HOLD_BYTES + i] = i;
    set_button_layers(0, disabled, sizeof(disabled));
    out = frame({ButtonCross});
    CHECK(out.DPad == None && buttons(out) == mask({ButtonCross}));
}

static uint32_t hold_bits(uint8_t n) {
    return layer(n).hold[0] | static_cast<uint32_t>(layer(n).hold[1]) << 8 |
           static_cast<uint32_t>(layer(n).hold[2]) << 16;
}

static void test_validation() {
    reset_config();
    Button &button = get_button();

    // Erased flash / older firmware (0xFF) or an earlier layout: all layers off.
    std::memset(&button.layers, 0xFF, sizeof(button.layers));
    button_layer_valid();
    CHECK(button.layers.format == BUTTON_LAYER_FORMAT);
    for (uint8_t n = 0; n < BUTTON_LAYER_COUNT; n++) {
        CHECK(hold_bits(n) == 0);
        for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) CHECK(layer(n).remap[i] == i);
    }
    std::memset(&button.layers, 0x19, sizeof(button.layers)); // old layout started with a ButtonId
    button_layer_valid();
    CHECK(hold_bits(0) == 0 && layer(0).remap[ButtonCross] == ButtonCross);

    // 0xFC carries layers 1 and 2: bits past RightPaddle are dropped, invalid
    // entries reset, layer buttons forced to identity in every layer.
    uint8_t data[2 * sizeof(ButtonLayer)];
    for (uint8_t n = 0; n < 2; n++) {
        uint8_t *entry = data + n * sizeof(ButtonLayer);
        entry[0] = entry[1] = entry[2] = 0;
        for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) entry[BUTTON_LAYER_HOLD_BYTES + i] = i;
    }
    data[2] = 0xF0 | (1u << (ButtonLeftPaddle - ButtonSquare - 16)); // junk bits 20-23 + LeftPaddle
    data[BUTTON_LAYER_HOLD_BYTES + ButtonCross] = DPadSouth;
    data[BUTTON_LAYER_HOLD_BYTES + ButtonSquare] = 200;
    data[BUTTON_LAYER_HOLD_BYTES + ButtonL2] = Disable;
    uint8_t *second = data + sizeof(ButtonLayer);
    const uint32_t l1_r1 = mask({ButtonL1, ButtonR1}) >> ButtonSquare;
    second[0] = l1_r1 & 0xFF;
    second[1] = (l1_r1 >> 8) & 0xFF;
    second[2] = (l1_r1 >> 16) & 0xFF;
    second[BUTTON_LAYER_HOLD_BYTES + ButtonLeftPaddle] = ButtonL3; // a layer button
    set_button_layers(0, data, sizeof(data));
    CHECK(button_layer_hold_mask(layer(0)) == mask({ButtonLeftPaddle}));
    CHECK(button_layer_hold_mask(layer(1)) == mask({ButtonL1, ButtonR1}));
    CHECK(layer(0).remap[ButtonCross] == DPadSouth);
    CHECK(layer(0).remap[ButtonSquare] == ButtonSquare);
    CHECK(layer(0).remap[ButtonL2] == Disable);
    CHECK(layer(1).remap[ButtonLeftPaddle] == ButtonLeftPaddle);

    // 0xFD carries layer 3; a hold set equal to an earlier layer's is dropped.
    uint8_t third[sizeof(ButtonLayer)];
    third[0] = second[0];
    third[1] = second[1];
    third[2] = 0;
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) third[BUTTON_LAYER_HOLD_BYTES + i] = i;
    third[BUTTON_LAYER_HOLD_BYTES + ButtonCross] = ButtonR3;
    set_button_layers(2, third, sizeof(third));
    CHECK(hold_bits(2) == 0);
    CHECK(layer(2).remap[ButtonCross] == ButtonR3); // the table is kept

    // A short write only changes the bytes it carries.
    uint8_t hold_only[1] = {1u << (ButtonTriangle - ButtonSquare)};
    set_button_layers(2, hold_only, sizeof(hold_only));
    CHECK(button_layer_hold_mask(layer(2)) == mask({ButtonTriangle}));
    CHECK(layer(2).remap[ButtonCross] == ButtonR3);
    CHECK(layer(0).remap[ButtonTriangle] == ButtonTriangle); // now a layer button everywhere

    // Oversized writes (63-byte reports) are clamped: 0xFC never reaches
    // layer 3, 0xFD never runs past it.
    uint8_t big[63];
    std::memset(big, 0, sizeof(big));
    set_button_layers(0, big, sizeof(big));
    CHECK(button_layer_hold_mask(layer(2)) == mask({ButtonTriangle}));
    CHECK(layer(2).remap[ButtonCross] == ButtonR3);
    set_button_layers(2, big, sizeof(big));
    CHECK(hold_bits(2) == 0);
    CHECK(button.guard == 0x5A);
}

static void test_combo_layers() {
    reset_config();
    combo_layers();

    // Each combination uses its own table.
    auto out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == 0 && out.DPad == South);
    out = frame({});
    out = frame({ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonL1}) && out.DPad == None);
    out = frame({});
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonR3}) && out.DPad == None);
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross, ButtonCircle});
    CHECK(buttons(out) == mask({ButtonR3}) && out.DPad == None); // Circle disabled in layer 3
    out = frame({});
    out = frame({ButtonCross});
    CHECK(buttons(out) == mask({ButtonCross}));
    out = frame({});

    // Exact match: layer 3 does not merge layer 1. Square has no layer 3
    // entry, so it falls back to base, not to layer 1's D-pad West.
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonSquare});
    CHECK(buttons(out) == mask({ButtonSquare}) && out.DPad == None);
    out = frame({});

    // Layer buttons are consumed, alone or together.
    out = frame({ButtonLeftPaddle, ButtonRightPaddle});
    CHECK(buttons(out) == 0 && out.DPad == None);
    out = frame({});
    out = frame({ButtonRightPaddle});
    CHECK(buttons(out) == 0);
    out = frame({});
}

static void test_combo_latching() {
    reset_config();
    combo_layers();

    // Releasing a paddle mid-press keeps Cross on layer 3 until it is released.
    auto out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonR3}));
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonR3}) && out.DPad == None);
    out = frame({ButtonLeftPaddle});
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == 0 && out.DPad == South); // a new press uses layer 1 now
    out = frame({});

    // Adding a paddle while a button latched to layer 1 is held: the held
    // button stays on layer 1, a new press uses layer 3.
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(out.DPad == South);
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross});
    CHECK(out.DPad == South && buttons(out) == 0);
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross, ButtonTriangle});
    CHECK(out.DPad == South && buttons(out) == mask({ButtonTriangle})); // no layer 3 entry -> base
    out = frame({});

    // Pressed before any layer button: base until released.
    out = frame({ButtonCross});
    out = frame({ButtonCross, ButtonLeftPaddle, ButtonRightPaddle});
    CHECK(buttons(out) == mask({ButtonCross}) && out.DPad == None);
    out = frame({});
}

static void test_chord_only_layer() {
    // A layer that needs two buttons, with no single-button layers.
    reset_config();
    set_hold(0, {ButtonL1, ButtonR1});
    layer(0).remap[ButtonCross] = ButtonHome;

    auto out = frame({ButtonL1, ButtonR1, ButtonCross});
    CHECK(buttons(out) == mask({ButtonHome}));
    out = frame({});
    // One of the two is not a match: base, and the hold buttons stay consumed.
    out = frame({ButtonL1, ButtonCross});
    CHECK(buttons(out) == mask({ButtonCross}));
    out = frame({});
    // Buttons outside every hold set do not break the match.
    out = frame({ButtonL1, ButtonR1, ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonHome, ButtonLeftPaddle}));
    out = frame({});
}

static void test_three_button_layer() {
    reset_config();
    set_hold(0, {ButtonLeftPaddle});
    layer(0).remap[ButtonCross] = ButtonL1;
    set_hold(2, {ButtonLeftPaddle, ButtonRightPaddle, ButtonL3});
    layer(2).remap[ButtonCross] = ButtonR3;

    auto out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonL3, ButtonCross});
    CHECK(buttons(out) == mask({ButtonR3}));
    out = frame({});
    // Left + right paddle matches no layer: base.
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonCross}));
    out = frame({});
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonL1}));
    out = frame({});
}

static void test_layer_two_alone() {
    // Only layer 2 set: it works on its own; the left paddle is a plain button.
    reset_config();
    combo_layers();
    set_hold(0, {});
    set_hold(2, {});
    auto out = frame({ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonL1}));
    out = frame({});
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonLeftPaddle, ButtonL1}));
    out = frame({});
}

int main() {
    test_identity_is_untouched();
    test_base_remap_unchanged();
    test_layer_basic();
    test_release_order();
    test_dpad_combining();
    test_swap_and_disable_in_layer();
    test_layer_inherits_base();
    test_modifier_rules();
    test_resets();
    test_validation();
    test_combo_layers();
    test_combo_latching();
    test_chord_only_layer();
    test_three_button_layer();
    test_layer_two_alone();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("layer_remap_test: all checks passed\n");
    return 0;
}
