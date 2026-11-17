//
// Created by awalol on 2026/8/14.
//

#ifndef DS5_BRIDGE_BUTTON_MAP_H
#define DS5_BRIDGE_BUTTON_MAP_H

#include <cstdint>

constexpr uint8_t BUTTON_REMAP_COUNT = 28;

// Button layer: while `modifier` is physically held, buttons pressed use
// `remap` instead of the base remap table (see docs/button-layer.md).
constexpr uint8_t BUTTON_LAYER_DISABLED = 0xFF;

struct __attribute__((packed)) ButtonLayer {
    uint8_t modifier; // ButtonId (a button, not a D-pad direction), or BUTTON_LAYER_DISABLED
    uint8_t remap[BUTTON_REMAP_COUNT]; // orig_btn:target_btn, same encoding as Button::button_remap
};

struct USBGetStateData;
void button_remap_apply(USBGetStateData& state_data);
// Forget which held buttons are latched to the layer (disconnect, layer config change).
void button_remap_reset();


#endif //DS5_BRIDGE_BUTTON_MAP_H
