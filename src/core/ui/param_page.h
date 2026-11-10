#pragma once

#include <stdint.h>

#include "voice.h"

class Canvas;
class Synth;

// Everything shown on the Elektron-style parameter page.
struct PageState {
    const Voice* voice = nullptr;
    int midiChannel = 0;  // 0-based
    int page = 0;
    uint8_t values[kParamsPerPage] = {};
    int lastTouched = -1;  // index within the page
    bool triggerFlash = false;

    bool operator==(const PageState& o) const;
};

void drawParamPage(Canvas& canvas, const PageState& state);

// Follows the synth's focused voice, shows the page of its last-touched
// parameter, and redraws only when something changed.
class PageView {
public:
    explicit PageView(const Synth& synth) : synth_(synth) {}

    // Returns true when `canvas` was redrawn and should be sent to the display.
    bool update(Canvas& canvas, uint32_t nowMs);

private:
    const Synth& synth_;
    PageState shown_;
    bool drawn_ = false;
    uint32_t seenTriggers_ = 0;
    uint32_t flashUntilMs_ = 0;
};
