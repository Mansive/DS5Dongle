//
// Created by awalol on 2026/8/14.
//

#ifndef DS5_BRIDGE_BUTTON_MAP_H
#define DS5_BRIDGE_BUTTON_MAP_H

#include <cstdint>

constexpr uint8_t BUTTON_REMAP_COUNT = 28;

// Button layers work like keyboard modifiers (see docs/button-layer.md). Each
// layer has a set of hold buttons. Every button in any layer's set is a layer
// button. A button pressed while the physically held layer buttons are exactly
// one layer's set uses that layer's table instead of the base table. An
// identity entry falls back to the base mapping.
constexpr uint8_t BUTTON_LAYER_COUNT = 3;
constexpr uint8_t BUTTON_LAYER_HOLD_BYTES = 3;
// ButtonLayers::format value; anything else (0xFF from older firmware, or an
// earlier layout) resets the layers to off.
constexpr uint8_t BUTTON_LAYER_FORMAT = 0x4C;

struct __attribute__((packed)) ButtonLayer {
    // Little-endian bitmask of hold buttons: bit n = ButtonId ButtonSquare + n.
    // Empty = layer off. D-pad directions cannot be hold buttons.
    uint8_t hold[BUTTON_LAYER_HOLD_BYTES];
    uint8_t remap[BUTTON_REMAP_COUNT]; // orig_btn:target_btn, same encoding as Button::button_remap
};

struct __attribute__((packed)) ButtonLayers {
    uint8_t format; // BUTTON_LAYER_FORMAT
    // Report 0xFC carries layers 1 and 2, report 0xFD carries layer 3.
    ButtonLayer layer[BUTTON_LAYER_COUNT];
};

// Hold set of a layer as a mask indexed by ButtonId (bit i = ButtonId i).
uint32_t button_layer_hold_mask(const ButtonLayer &layer);

struct USBGetStateData;
void button_remap_apply(USBGetStateData& state_data);
// Mouse buttons (bit 0 = MouseLeft ... bit 4 = MouseForward) held by sources
// mapped to mouse targets, as of the last button_remap_apply() call.
uint8_t button_remap_mouse_buttons();
// Whether the base table or any layer maps a button to a mouse target.
bool button_remap_uses_mouse();
// Forget which layer held buttons are latched to (disconnect, layer config change).
void button_remap_reset();


#endif //DS5_BRIDGE_BUTTON_MAP_H
