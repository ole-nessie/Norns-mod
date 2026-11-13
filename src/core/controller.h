#pragma once

#include <stdint.h>

struct MidiMessage;
class Synth;

// The BeatStep Pro sends every control on this channel (MIDI channel 16).
constexpr int kControllerChannel = 15;

// Arturia MIDI Control Center "Relative #1": 64 means no change, 65 is +1,
// 63 is -1, and larger turns send a bigger step. "Relative #2" is the 7-bit
// two's-complement form (1 = +1, 127 = -1).
enum class RelativeEncoding { Arturia1, Arturia2 };
constexpr RelativeEncoding kRelativeEncoding = RelativeEncoding::Arturia1;

int decodeRelative(uint8_t value);

// Called on the audio side for one message. Channel 16 is consumed here.
// Returns true when the message was on the controller channel.
bool applyController(Synth& synth, const MidiMessage& msg);
