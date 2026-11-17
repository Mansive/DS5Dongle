//
// Created by awalol on 2026/8/14.
//

#ifndef DS5_BRIDGE_BUTTON_MAP_H
#define DS5_BRIDGE_BUTTON_MAP_H

#include <cstdint>

constexpr uint8_t BUTTON_REMAP_COUNT = 28;

// Button layers work like keyboard modifiers (see docs/button-layer.md). There
// are two layer buttons, A and B. Buttons pressed while they are physically
// held use the table for that exact combination instead of the base table:
// A alone -> ButtonLayer::remap, B alone -> ButtonLayer2::remap, A and B ->
// ButtonLayer2::combo_remap. An identity entry falls back to the base mapping.
constexpr uint8_t BUTTON_LAYER_DISABLED = 0xFF;

// Held layer buttons as a bitmask; also the layer a press is latched to.
constexpr uint8_t BUTTON_LAYER_HELD_A = 0x01;
constexpr uint8_t BUTTON_LAYER_HELD_B = 0x02;

// Layer button A and its table (report 0xFC).
struct __attribute__((packed)) ButtonLayer {
    uint8_t modifier; // ButtonId (a button, not a D-pad direction), or BUTTON_LAYER_DISABLED
    uint8_t remap[BUTTON_REMAP_COUNT]; // orig_btn:target_btn, same encoding as Button::button_remap
};

// Layer button B, its table, and the table for A and B held together (report 0xFD).
struct __attribute__((packed)) ButtonLayer2 {
    uint8_t modifier; // ButtonId, or BUTTON_LAYER_DISABLED; never equal to ButtonLayer::modifier
    uint8_t remap[BUTTON_REMAP_COUNT];       // B held alone
    uint8_t combo_remap[BUTTON_REMAP_COUNT]; // A and B held together
};

struct USBGetStateData;
void button_remap_apply(USBGetStateData& state_data);
// Forget which layer held buttons are latched to (disconnect, layer config change).
void button_remap_reset();


#endif //DS5_BRIDGE_BUTTON_MAP_H
