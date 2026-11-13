#include "machines.h"

#include <string.h>

const char* machineCode(uint8_t kind) {
    if (kind == kMachineEfm) return "EFMBD";
    if (kind == kMachineSid) return "SID";
    if (kind == kMachineMetal) return "METL";
    if (kind == kMachineFm) return "FM2";
    if (kind == kMachine808) return "A808";
    if (kind == kMachineEfmDrum) return "EFMD";
    if (kind == kMachineFm4) return "FM4";
    if (kind == kMachine303) return "A303";
    if (kind == kMachineChord) return "CHRD";
    return "";
}

const char* machineName(uint8_t kind) {
    if (kind == kMachineEfm) return "EFM BD";
    if (kind == kMachineSid) return "SID";
    if (kind == kMachineMetal) return "METAL";
    if (kind == kMachineFm) return "FM";
    if (kind == kMachine808) return "808";
    if (kind == kMachineEfmDrum) return "EFM DR";
    if (kind == kMachineFm4) return "FM4";
    if (kind == kMachine303) return "303";
    if (kind == kMachineChord) return "CHORD";
    return "EMPTY";
}

uint8_t machineKind(const char* code) {
    if (code && strcmp(code, "EFMBD") == 0) return kMachineEfm;
    if (code && strcmp(code, "SID") == 0) return kMachineSid;
    if (code && strcmp(code, "METL") == 0) return kMachineMetal;
    if (code && strcmp(code, "FM2") == 0) return kMachineFm;
    if (code && strcmp(code, "A808") == 0) return kMachine808;
    if (code && strcmp(code, "EFMD") == 0) return kMachineEfmDrum;
    if (code && strcmp(code, "FM4") == 0) return kMachineFm4;
    if (code && strcmp(code, "A303") == 0) return kMachine303;
    if (code && strcmp(code, "CHRD") == 0) return kMachineChord;
    return kMachineNone;
}

int machineParamCount(uint8_t kind) {
    if (kind == kMachineEfm) return kParamsPerPage;
    if (kind == kMachineSid) return SidVoice::kParamCount;
    if (kind == kMachineMetal) return kParamsPerPage;
    if (kind == kMachineFm) return FmVoice::kParamCount;
    if (kind == kMachine808) return kParamsPerPage;
    if (kind == kMachineEfmDrum) return kParamsPerPage;
    if (kind == kMachineFm4) return Fm4Voice::kParamCount;
    if (kind == kMachine303) return kParamsPerPage;
    if (kind == kMachineChord) return kParamsPerPage;
    return 0;
}

void MachinePool::prepare(float sampleRate) {
    for (int i = 0; i < kChannels; ++i) {
        kicks_[i].prepare(sampleRate);
        sids_[i].prepare(sampleRate);
        metals_[i].prepare(sampleRate);
        fms_[i].prepare(sampleRate);
        analogs_[i].prepare(sampleRate);
        efmDrums_[i].prepare(sampleRate);
        fm4s_[i].prepare(sampleRate);
        acids_[i].prepare(sampleRate);
        chords_[i].prepare(sampleRate);
    }
}

Voice* MachinePool::voice(int channel, uint8_t kind) {
    if (channel < 0 || channel >= kChannels) return nullptr;
    if (kind == kMachineEfm) return &kicks_[channel];
    if (kind == kMachineSid) return &sids_[channel];
    if (kind == kMachineMetal) return &metals_[channel];
    if (kind == kMachineFm) return &fms_[channel];
    if (kind == kMachine808) return &analogs_[channel];
    if (kind == kMachineEfmDrum) return &efmDrums_[channel];
    if (kind == kMachineFm4) return &fm4s_[channel];
    if (kind == kMachine303) return &acids_[channel];
    if (kind == kMachineChord) return &chords_[channel];
    return nullptr;
}
