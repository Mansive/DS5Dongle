//
// BOOTSEL button gestures, split out of bt.cpp.
//

#ifndef DS5_BRIDGE_BUTTON_FUNCTIONS_H
#define DS5_BRIDGE_BUTTON_FUNCTIONS_H

// Poll the BOOTSEL button at 10 Hz and dispatch gestures:
//   single click  -> pair another controller   (bt_bootsel_click_action)
//   double click  -> reboot the Pico
//   triple click  -> reboot into BOOTSEL (USB mass storage)
//   ~1.5 s hold   -> clear all pairings         (bt_bootsel_hold_action)
// Also performs a reboot requested with bootsel_reboot_request().
void button_check();

// Reboot into BOOTSEL shortly (~200 ms) from the main loop, e.g. from a USB
// command handler that must return before the device goes away.
void bootsel_reboot_request();

#endif // DS5_BRIDGE_BUTTON_FUNCTIONS_H
