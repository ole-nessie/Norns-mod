#pragma once

#include "acid_voice.h"
#include "analog_drum.h"
#include "chord_voice.h"
#include "efm_drum.h"
#include "efm_kick.h"
#include "fm4_voice.h"
#include "fm_voice.h"
#include "metal_voice.h"
#include "sid_voice.h"

// MIDI channels 1-15. Channel 16 is the BeatStep Pro control surface.
constexpr int kChannels = 15;
constexpr uint8_t kMachineNone = 0;
constexpr uint8_t kMachineEfm = 1;
constexpr uint8_t kMachineSid = 2;
constexpr uint8_t kMachineMetal = 3;
constexpr uint8_t kMachineFm = 4;
constexpr uint8_t kMachine808 = 5;
constexpr uint8_t kMachineEfmDrum = 6;
constexpr uint8_t kMachineFm4 = 7;
constexpr uint8_t kMachine303 = 8;
constexpr uint8_t kMachineChord = 9;

const char* machineCode(uint8_t kind);
const char* machineName(uint8_t kind);
uint8_t machineKind(const char* code);
int machineParamCount(uint8_t kind);

// One of each machine per MIDI channel, prepared before audio starts.
class MachinePool {
public:
    void prepare(float sampleRate);
    Voice* voice(int channel, uint8_t kind);

private:
    EfmKick kicks_[kChannels];
    SidVoice sids_[kChannels];
    MetalVoice metals_[kChannels];
    FmVoice fms_[kChannels];
    AnalogDrum analogs_[kChannels];
    EfmDrum efmDrums_[kChannels];
    Fm4Voice fm4s_[kChannels];
    AcidVoice acids_[kChannels];
    ChordVoice chords_[kChannels];
};
