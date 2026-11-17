//
// Created by awalol on 2026/8/14.
//

#include <cstdint>

#include "button_remap.h"
#include "button_utils.h"
#include "config.h"
#include "utils.h"

static_assert(Disable <= 32, "pressed/latched masks hold one bit per ButtonId");

// D-pad output is collected as direction bits so several sources (and the
// physical D-pad) combine into diagonals instead of overwriting each other.
constexpr uint8_t DPAD_UP = 0x01;
constexpr uint8_t DPAD_RIGHT = 0x02;
constexpr uint8_t DPAD_DOWN = 0x04;
constexpr uint8_t DPAD_LEFT = 0x08;

// Indexed by Direction (North..NorthWest, None). DPad ButtonIds share these values.
constexpr uint8_t DIRECTION_BITS[] = {
    DPAD_UP, DPAD_UP | DPAD_RIGHT, DPAD_RIGHT, DPAD_DOWN | DPAD_RIGHT,
    DPAD_DOWN, DPAD_DOWN | DPAD_LEFT, DPAD_LEFT, DPAD_UP | DPAD_LEFT,
    0,
};
static_assert(sizeof(DIRECTION_BITS) == None + 1);

// Physical buttons held on the previous call, and the held buttons that were
// pressed while the layer modifier was down. Both are derived from state, so
// re-applying the same input (polling modes 0-2 resend one BT report many
// times) never changes them.
static uint32_t prev_pressed = 0;
static uint32_t layer_latched = 0;

static uint8_t direction_bits(const uint8_t direction) {
    return direction < sizeof(DIRECTION_BITS) ? DIRECTION_BITS[direction] : 0;
}

static Direction bits_to_direction(uint8_t bits) {
    // Opposite directions cancel (SOCD neutral).
    if ((bits & (DPAD_UP | DPAD_DOWN)) == (DPAD_UP | DPAD_DOWN)) bits &= ~(DPAD_UP | DPAD_DOWN);
    if ((bits & (DPAD_LEFT | DPAD_RIGHT)) == (DPAD_LEFT | DPAD_RIGHT)) bits &= ~(DPAD_LEFT | DPAD_RIGHT);
    for (uint8_t direction = North; direction <= NorthWest; direction++) {
        if (DIRECTION_BITS[direction] == bits) return static_cast<Direction>(direction);
    }
    return None;
}

static void disable_state_value(USBGetStateData &state_data, int index) {
    switch (index) {
        case DPadNorth:
        case DPadNorthEast:
        case DPadEast:
        case DPadSouthEast:
        case DPadSouth:
        case DPadSouthWest:
        case DPadWest:
        case DPadNorthWest:
            if (state_data.DPad == index) {
                state_data.DPad = None;
            }
            break;
        case ButtonSquare: state_data.ButtonSquare = 0; break;
        case ButtonCross: state_data.ButtonCross = 0; break;
        case ButtonCircle: state_data.ButtonCircle = 0; break;
        case ButtonTriangle: state_data.ButtonTriangle = 0; break;
        case ButtonL1: state_data.ButtonL1 = 0; break;
        case ButtonR1: state_data.ButtonR1 = 0; break;
        case ButtonL2: state_data.ButtonL2 = 0; break;
        case ButtonR2: state_data.ButtonR2 = 0; break;
        case ButtonCreate: state_data.ButtonCreate = 0; break;
        case ButtonOptions: state_data.ButtonOptions = 0; break;
        case ButtonL3: state_data.ButtonL3 = 0; break;
        case ButtonR3: state_data.ButtonR3 = 0; break;
        case ButtonHome: state_data.ButtonHome = 0; break;
        case ButtonPad: state_data.ButtonPad = 0; break;
        case ButtonMute: state_data.ButtonMute = 0; break;
        case ButtonLeftFunction: state_data.ButtonLeftFunction = 0; break;
        case ButtonRightFunction: state_data.ButtonRightFunction = 0; break;
        case ButtonLeftPaddle: state_data.ButtonLeftPaddle = 0; break;
        case ButtonRightPaddle: state_data.ButtonRightPaddle = 0; break;
        default: break;
    }
}

static void press_button(USBGetStateData &state_data, const uint8_t button) {
    switch (button) {
        case ButtonSquare: state_data.ButtonSquare = 1; break;
        case ButtonCross: state_data.ButtonCross = 1; break;
        case ButtonCircle: state_data.ButtonCircle = 1; break;
        case ButtonTriangle: state_data.ButtonTriangle = 1; break;
        case ButtonL1: state_data.ButtonL1 = 1; break;
        case ButtonR1: state_data.ButtonR1 = 1; break;
        case ButtonL2: state_data.ButtonL2 = 1; break;
        case ButtonR2: state_data.ButtonR2 = 1; break;
        case ButtonCreate: state_data.ButtonCreate = 1; break;
        case ButtonOptions: state_data.ButtonOptions = 1; break;
        case ButtonL3: state_data.ButtonL3 = 1; break;
        case ButtonR3: state_data.ButtonR3 = 1; break;
        case ButtonHome: state_data.ButtonHome = 1; break;
        case ButtonPad: state_data.ButtonPad = 1; break;
        case ButtonMute: state_data.ButtonMute = 1; break;
        case ButtonLeftFunction: state_data.ButtonLeftFunction = 1; break;
        case ButtonRightFunction: state_data.ButtonRightFunction = 1; break;
        case ButtonLeftPaddle: state_data.ButtonLeftPaddle = 1; break;
        case ButtonRightPaddle: state_data.ButtonRightPaddle = 1; break;
        default: break;
    }
}

void button_remap_reset() {
    prev_pressed = 0;
    layer_latched = 0;
}

void button_remap_apply(USBGetStateData &state_data) {
    // key: physical source button, value: target button it is reported as.
    // While the layer modifier is held, newly pressed buttons latch to the
    // layer table and keep using it until they are released, whatever the
    // modifier does in the meantime.
    const Button &button = get_button();
    const ButtonLayer &layer = button.layer;
    const USBGetStateData state_copy = state_data;

    uint32_t pressed = 0;
    for (uint8_t i = 0; i < Disable; i++) {
        if (button_is_pressed(state_copy, i)) {
            pressed |= 1u << i;
        }
    }

    // The modifier is read from the physical state only, so mapping another
    // button *to* it never activates the layer.
    const bool layer_enabled = layer.modifier < Disable; // BUTTON_LAYER_DISABLED is above Disable
    if (layer_enabled) {
        if (pressed & (1u << layer.modifier)) {
            layer_latched |= pressed & ~prev_pressed;
        }
        layer_latched &= pressed;
    } else {
        layer_latched = 0;
    }
    prev_pressed = pressed;

    uint8_t targets[Disable];
    bool remapped = false;
    for (uint8_t i = 0; i < Disable; i++) {
        // A layer entry left at identity inherits the base mapping.
        const bool use_layer = (layer_latched & (1u << i)) && layer.remap[i] != i;
        uint8_t target = use_layer ? layer.remap[i] : button.button_remap[i];
        if (layer_enabled && i == layer.modifier) {
            target = Disable; // The modifier is consumed; its base mapping is ignored.
        }
        targets[i] = target;
        remapped |= target != i;
    }
    if (!remapped) {
        return;
    }

    // Clear every remapped physical source first. Targets are populated from
    // state_copy below, so mappings do not depend on enum order.
    for (uint8_t i = 0; i < Disable; i++) {
        if (targets[i] != i) {
            disable_state_value(state_data, i);
        }
    }

    uint8_t dpad = direction_bits(state_data.DPad); // What is left of the physical D-pad.
    bool dpad_targeted = false;
    for (uint8_t i = 0; i < Disable; i++) {
        const uint8_t target = targets[i];
        if (target == i || target >= Disable || !(pressed & (1u << i))) {
            continue;
        }
        if (target <= DPadNorthWest) {
            dpad |= direction_bits(target);
            dpad_targeted = true;
        } else {
            press_button(state_data, target);
        }
    }
    if (dpad_targeted) {
        state_data.DPad = bits_to_direction(dpad);
    }
}
