#include "controller.h"

#include "midi.h"
#include "synth.h"

namespace {

// Matches docs/beatstep-pro-setup.md. Number buttons 1-15 send these notes.
constexpr uint8_t kFirstChannelButton = 52;  // E2, machine 1; button 15 is F#3
constexpr uint8_t kPanicButton = 67;         // number button 16, G3
constexpr uint8_t kFirstFxPad = 36;          // top row pads 1-3: delay, reverb, chorus
constexpr uint8_t kFirstPagePad = 44;        // bottom row pads 1-4
constexpr uint8_t kAuditionPad = 51;         // bottom row pad 8

int encoderSlot(uint8_t cc) {
    if (cc >= 1 && cc <= 4) return cc - 1;          // top row, display slots 0-3
    if (cc >= 9 && cc <= 12) return cc - 5;         // bottom row, display slots 4-7
    return -1;
}

}  // namespace

int decodeRelative(uint8_t value) {
    if (kRelativeEncoding == RelativeEncoding::Arturia1) return static_cast<int>(value) - 64;
    return value < 64 ? value : static_cast<int>(value) - 128;
}

bool applyController(Synth& synth, const MidiMessage& msg) {
    if (msg.channel() != kControllerChannel) return false;

    if (msg.type() == 0xB0) {
        if (msg.data1 == 120 || msg.data1 == 123) {
            synth.panic();
            return true;
        }
        const int delta = decodeRelative(msg.data2);
        if (msg.data1 == 5) {
            if (delta != 0) synth.stepMachine(delta);
            return true;
        }
        if (msg.data1 == 6) {
            if (delta != 0) synth.addKitDelta(synth.focusedChannel(), delta);
            return true;
        }
        const int slot = encoderSlot(msg.data1);
        if (slot >= 0 && delta != 0) synth.nudgeFocusedParam(slot, delta);
        return true;
    }

    const bool noteOn = msg.type() == 0x90 && msg.data2 > 0;
    const bool noteOff = msg.type() == 0x80 || (msg.type() == 0x90 && msg.data2 == 0);
    if (!noteOn && !noteOff) return true;

    if (msg.data1 == kAuditionPad) {
        synth.audition(noteOn);
        return true;
    }
    if (!noteOn) return true;

    if (msg.data1 == kPanicButton) {
        synth.panic();
    } else if (msg.data1 >= kFirstChannelButton && msg.data1 < kFirstChannelButton + kChannels) {
        synth.focusChannel(msg.data1 - kFirstChannelButton);
    } else if (msg.data1 >= kFirstFxPad && msg.data1 < kFirstFxPad + kFxPages) {
        synth.focusFxPage(msg.data1 - kFirstFxPad);
    } else if (msg.data1 >= kFirstPagePad && msg.data1 < kFirstPagePad + 4) {
        const int pad = msg.data1 - kFirstPagePad;
        const int ch = synth.focusedChannel();
        // Pad 4 is the channel mix on every machine. Pads 1-3 are its sound pages.
        if (pad == 3) synth.focusPage(synth.voicePages(ch));
        else if (pad < synth.voicePages(ch)) synth.focusPage(pad);
    }
    return true;
}
