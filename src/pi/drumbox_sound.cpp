#include "drumbox_sound.h"

#include "synth.h"

DrumboxSound::DrumboxSound(CInterruptSystem* interrupt, Synth* synth)
    : CI2SSoundBaseDevice(interrupt, kSampleRate, kChunkWords, TRUE /* slave */),
      synth_(synth) {}

unsigned DrumboxSound::GetChunk(u32* buffer, unsigned chunkSize) {
    if (chunkSize > kChunkWords) chunkSize = kChunkWords;
    synth_->process(scratch_, static_cast<int>(chunkSize / 2), 2);

    const int max = GetRangeMax();  // 24-bit samples in 32-bit words
    for (unsigned i = 0; i < chunkSize; ++i) {
        buffer[i] = static_cast<u32>(static_cast<int>(scratch_[i] * max));
    }
    return chunkSize;
}
