#pragma once

#include <circle/gpiopin.h>
#include <circle/types.h>

#include "panel_event.h"

// Norns shield keys and encoders. Keys are active low.
// E1 GPIO 4/27, E2 24/23, E3 12/25. K1 GPIO 22, K2 26, K3 13.
class Panel {
public:
    void begin();
    // Appends events and returns how many were written.
    int poll(PanelEvent* out, int max, unsigned nowMs);

private:
    CGPIOPin a_[3];
    CGPIOPin b_[3];
    CGPIOPin key_[3];
    u8 enc_[3] = {};
    int accum_[3] = {};
    bool down_[3] = {};
    unsigned changedMs_[3] = {};
    bool begun_ = false;
};
