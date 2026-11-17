// Host test for src/button_remap.cpp (button layer). Built by tests/test_layer.py
// against the real USBGetStateData layout; see that file for the stubs.
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "button_remap.h"
#include "button_utils.h"
#include "config.h"
#include "utils.h"

void set_button_layer(const uint8_t *new_layer, uint16_t len);
void set_button_layer2(const uint8_t *new_layer, uint16_t len);

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

static void reset_config() {
    Button &button = get_button();
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) {
        button.button_remap[i] = i;
        button.layer.remap[i] = i;
        button.layer2.remap[i] = i;
        button.layer2.combo_remap[i] = i;
    }
    button.layer.modifier = BUTTON_LAYER_DISABLED;
    button.layer2.modifier = BUTTON_LAYER_DISABLED;
    button_remap_reset();
}

// A = left paddle (face buttons -> D-pad), B = right paddle (Cross -> L1),
// A+B (Cross -> R3, Circle -> Disable).
static void combo_layers() {
    Button &button = get_button();
    button.layer.modifier = ButtonLeftPaddle;
    button.layer.remap[ButtonTriangle] = DPadNorth;
    button.layer.remap[ButtonCross] = DPadSouth;
    button.layer.remap[ButtonSquare] = DPadWest;
    button.layer.remap[ButtonCircle] = DPadEast;
    button.layer2.modifier = ButtonRightPaddle;
    button.layer2.remap[ButtonCross] = ButtonL1;
    button.layer2.combo_remap[ButtonCross] = ButtonR3;
    button.layer2.combo_remap[ButtonCircle] = Disable;
}

static void face_dpad_layer(uint8_t modifier = ButtonLeftPaddle) {
    Button &button = get_button();
    button.layer.modifier = modifier;
    button.layer.remap[ButtonTriangle] = DPadNorth;
    button.layer.remap[ButtonCross] = DPadSouth;
    button.layer.remap[ButtonSquare] = DPadWest;
    button.layer.remap[ButtonCircle] = DPadEast;
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
    button.layer.modifier = ButtonLeftPaddle;
    button.layer.remap[ButtonCross] = DPadSouth;
    button.layer.remap[ButtonSquare] = ButtonCross;
    button.layer.remap[ButtonL1] = Disable;

    auto out = frame({ButtonLeftPaddle, ButtonSquare, ButtonCross, ButtonL1});
    CHECK(out.DPad == South && buttons(out) == mask({ButtonCross}));
}

static void test_layer_inherits_base() {
    reset_config();
    Button &button = get_button();
    button.button_remap[ButtonSquare] = ButtonTriangle;
    face_dpad_layer();
    button.layer.remap[ButtonSquare] = ButtonSquare; // no layer entry

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
    uint8_t disabled[1 + BUTTON_REMAP_COUNT];
    disabled[0] = BUTTON_LAYER_DISABLED;
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) disabled[1 + i] = i;
    set_button_layer(disabled, sizeof(disabled));
    out = frame({ButtonCross});
    CHECK(out.DPad == None && buttons(out) == mask({ButtonCross}));
}

static void test_validation() {
    reset_config();
    ButtonLayer &layer = get_button().layer;

    // Erased flash / old firmware: all 0xFF.
    uint8_t ff[sizeof(ButtonLayer)];
    std::memset(ff, 0xFF, sizeof(ff));
    set_button_layer(ff, sizeof(ff));
    CHECK(layer.modifier == BUTTON_LAYER_DISABLED);
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) CHECK(layer.remap[i] == i);

    // D-pad directions and out-of-range ids are not valid modifiers.
    const uint8_t bad_modifiers[] = {DPadNorth, DPadNorthWest, Disable, 0x80};
    for (const uint8_t bad : bad_modifiers) {
        uint8_t data[1] = {bad};
        set_button_layer(data, sizeof(data));
        CHECK(layer.modifier == BUTTON_LAYER_DISABLED);
    }

    // Valid write; invalid entries reset; the modifier's own entry is forced to identity.
    uint8_t data[sizeof(ButtonLayer)];
    data[0] = ButtonLeftPaddle;
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) data[1 + i] = i;
    data[1 + ButtonCross] = DPadSouth;
    data[1 + ButtonSquare] = 200;
    data[1 + ButtonLeftPaddle] = ButtonL1;
    data[1 + ButtonL2] = Disable;
    set_button_layer(data, sizeof(data));
    CHECK(layer.modifier == ButtonLeftPaddle);
    CHECK(layer.remap[ButtonCross] == DPadSouth);
    CHECK(layer.remap[ButtonSquare] == ButtonSquare);
    CHECK(layer.remap[ButtonLeftPaddle] == ButtonLeftPaddle);
    CHECK(layer.remap[ButtonL2] == Disable);

    // A short write only changes the bytes it carries.
    uint8_t modifier_only[1] = {ButtonRightPaddle};
    set_button_layer(modifier_only, sizeof(modifier_only));
    CHECK(layer.modifier == ButtonRightPaddle);
    CHECK(layer.remap[ButtonCross] == DPadSouth);

    // Oversized write (63-byte report) is clamped to the struct.
    uint8_t big[63];
    std::memset(big, 0xAB, sizeof(big));
    big[0] = ButtonLeftPaddle;
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) big[1 + i] = i;
    set_button_layer(big, sizeof(big));
    CHECK(layer.modifier == ButtonLeftPaddle);
    CHECK(get_button().guard_a == 0x5A);
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
    CHECK(buttons(out) == mask({ButtonR3}) && out.DPad == None); // Circle disabled in A+B
    out = frame({});
    out = frame({ButtonCross});
    CHECK(buttons(out) == mask({ButtonCross}));
    out = frame({});

    // Exact match: A+B does not merge A's table. Square has no A+B entry, so
    // it falls back to base, not to A's D-pad West.
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonSquare});
    CHECK(buttons(out) == mask({ButtonSquare}) && out.DPad == None);
    out = frame({});

    // Both layer buttons are consumed, alone or together.
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

    // Releasing B mid-press keeps Cross on the A+B table until it is released.
    auto out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonR3}));
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonR3}) && out.DPad == None);
    out = frame({ButtonLeftPaddle});
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(buttons(out) == 0 && out.DPad == South); // new press uses A now
    out = frame({});

    // Pressing B while a button latched to A is held: the held button stays
    // on A, a new press uses A+B.
    out = frame({ButtonLeftPaddle, ButtonCross});
    CHECK(out.DPad == South);
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross});
    CHECK(out.DPad == South && buttons(out) == 0);
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross, ButtonTriangle});
    CHECK(out.DPad == South && buttons(out) == mask({ButtonTriangle})); // Triangle: no A+B entry -> base
    out = frame({});

    // Pressed before any layer button: base until released.
    out = frame({ButtonCross});
    out = frame({ButtonCross, ButtonLeftPaddle, ButtonRightPaddle});
    CHECK(buttons(out) == mask({ButtonCross}) && out.DPad == None);
    out = frame({});
}

static void test_layer_b_alone() {
    // Only layer button B set: B works on its own, A+B is unreachable.
    reset_config();
    combo_layers();
    get_button().layer.modifier = BUTTON_LAYER_DISABLED;
    auto out = frame({ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonL1}));
    out = frame({});
    // The left paddle is a plain button again.
    out = frame({ButtonLeftPaddle, ButtonRightPaddle, ButtonCross});
    CHECK(buttons(out) == mask({ButtonLeftPaddle, ButtonL1}));
    out = frame({});
}

static void test_validation_layer2() {
    reset_config();
    Button &button = get_button();
    ButtonLayer2 &layer2 = button.layer2;

    // Erased flash / older firmware: all 0xFF.
    uint8_t ff[sizeof(ButtonLayer2)];
    std::memset(ff, 0xFF, sizeof(ff));
    set_button_layer2(ff, sizeof(ff));
    CHECK(layer2.modifier == BUTTON_LAYER_DISABLED);
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) {
        CHECK(layer2.remap[i] == i);
        CHECK(layer2.combo_remap[i] == i);
    }

    // D-pad directions and out-of-range ids are not valid layer buttons.
    const uint8_t bad_modifiers[] = {DPadSouth, Disable, 0x80};
    for (const uint8_t bad : bad_modifiers) {
        uint8_t data[1] = {bad};
        set_button_layer2(data, sizeof(data));
        CHECK(layer2.modifier == BUTTON_LAYER_DISABLED);
    }

    // B may not duplicate A.
    button.layer.modifier = ButtonLeftPaddle;
    uint8_t same[1] = {ButtonLeftPaddle};
    set_button_layer2(same, sizeof(same));
    CHECK(layer2.modifier == BUTTON_LAYER_DISABLED);

    // Both layer buttons are forced to identity in all three tables.
    uint8_t data[sizeof(ButtonLayer2)];
    data[0] = ButtonRightPaddle;
    for (uint8_t i = 0; i < BUTTON_REMAP_COUNT; i++) {
        data[1 + i] = i;
        data[1 + BUTTON_REMAP_COUNT + i] = i;
    }
    data[1 + ButtonCross] = ButtonL1;
    data[1 + ButtonLeftPaddle] = ButtonL2;
    data[1 + BUTTON_REMAP_COUNT + ButtonCross] = ButtonR3;
    data[1 + BUTTON_REMAP_COUNT + ButtonRightPaddle] = ButtonL3;
    data[1 + BUTTON_REMAP_COUNT + ButtonSquare] = 200;
    button.layer.remap[ButtonRightPaddle] = ButtonR1;
    set_button_layer2(data, sizeof(data));
    CHECK(layer2.modifier == ButtonRightPaddle);
    CHECK(layer2.remap[ButtonCross] == ButtonL1);
    CHECK(layer2.remap[ButtonLeftPaddle] == ButtonLeftPaddle);
    CHECK(layer2.combo_remap[ButtonCross] == ButtonR3);
    CHECK(layer2.combo_remap[ButtonRightPaddle] == ButtonRightPaddle);
    CHECK(layer2.combo_remap[ButtonSquare] == ButtonSquare);
    CHECK(button.layer.remap[ButtonRightPaddle] == ButtonRightPaddle);

    // Moving A onto B's button disables B (the newest write wins).
    uint8_t a_right[1] = {ButtonRightPaddle};
    set_button_layer(a_right, sizeof(a_right));
    CHECK(button.layer.modifier == ButtonRightPaddle);
    CHECK(layer2.modifier == BUTTON_LAYER_DISABLED);

    // Oversized write (63-byte report) is clamped to the struct.
    uint8_t big[63];
    std::memset(big, 0xAB, sizeof(big));
    big[0] = ButtonLeftPaddle;
    set_button_layer2(big, sizeof(big));
    CHECK(button.guard == 0x5A);
    CHECK(button.guard_a == 0x5A);
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
    test_layer_b_alone();
    test_validation_layer2();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("layer_remap_test: all checks passed\n");
    return 0;
}
