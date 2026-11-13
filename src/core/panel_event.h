#pragma once

#include <stdint.h>

// A turn or a press from the norns panel. The Pi fills these from GPIO;
// the desktop simulator fills them from the keyboard.
enum class PanelControl : uint8_t { Encoder, Key };

struct PanelEvent {
    PanelControl control = PanelControl::Key;
    uint8_t index = 0;  // 0..2
    int8_t delta = 0;   // encoder steps, or 1 when a key is pressed
};
