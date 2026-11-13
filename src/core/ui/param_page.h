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
    bool master = false;  // master effect page
    bool strip = false;   // channel volume and sends
    char kitName[9] = {};  // shown in place of the machine name after a kit encoder load

    bool operator==(const PageState& o) const;
};

void drawParamPage(Canvas& canvas, const PageState& state);

// Follows the synth's focused voice and page, and redraws only when
// something changed. The highlighted cell is the last parameter edited on
// the page now showing.
class PageView {
public:
    explicit PageView(const Synth& synth) : synth_(synth) {}

    // Returns true when `canvas` was redrawn and should be sent to the display.
    bool update(Canvas& canvas, uint32_t nowMs);
    void invalidate() { drawn_ = false; }

private:
    const Synth& synth_;
    PageState shown_;
    bool drawn_ = false;
    uint32_t seenTriggers_ = 0;
    uint32_t flashUntilMs_ = 0;
};
