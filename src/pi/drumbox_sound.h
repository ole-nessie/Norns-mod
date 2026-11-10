#pragma once

#include <circle/interrupt.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/types.h>

class Synth;

constexpr unsigned kSampleRate = 48000;
// Words per DMA chunk (stereo samples): 128 frames, about 2.7 ms.
constexpr unsigned kChunkWords = 256;

// I2S output in slave mode; the CS4270 supplies bit and frame clocks.
// GetChunk runs in the DMA completion IRQ and renders the synth directly.
class DrumboxSound : public CI2SSoundBaseDevice {
public:
    DrumboxSound(CInterruptSystem* interrupt, Synth* synth);

    unsigned GetChunk(u32* buffer, unsigned chunkSize) override;

private:
    Synth* synth_;
    float scratch_[kChunkWords];
};
