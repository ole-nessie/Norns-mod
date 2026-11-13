#pragma once

#include "voice.h"

// EFM drum kit. A swept sine, a decaying phase modulator and a noise partial,
// with the model picking the balance. Independent of the single EFM kick and
// not a port of any commercial drum machine.
class EfmDrum : public Voice {
public:
    enum Param { MODL, TUNE, DEC, RAMP, TONE, NOIS, MOD, VOL };

    const char* name() const override { return "EFMD"; }
    const ParamInfo* params() const override;
    const char* formatValue(int index, uint8_t value, char* buf) const override;

    void prepare(float sampleRate) override;
    void setParam(int index, uint8_t value) override;
    void noteOn(uint8_t note, float velocity) override;
    void render(float* out, int frames) override;

private:
    void updateDerived();
    float noise();

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamsPerPage] = {};

    int model_ = 0;
    float baseHz_ = 50.0f;
    float tuneSemis_ = 0.0f;
    float sweep_ = 0.0f;
    float modIndex_ = 0.0f;
    float modRatio_ = 1.0f;
    float bodyMix_ = 1.0f;
    float noiseMix_ = 0.0f;
    float gain_ = 0.5f;
    float ampCoef_ = 0.0f;
    float pitchCoef_ = 0.0f;
    float modCoef_ = 0.0f;
    float toneCoef_ = 0.2f;
    int bursts_ = 0;

    bool active_ = false;
    uint8_t note_ = 60;
    float velocity_ = 0.0f;
    float ampEnv_ = 0.0f;
    float pitchEnv_ = 0.0f;
    float modEnv_ = 0.0f;
    float carPhase_ = 0.0f;
    float modPhase_ = 0.0f;
    float noiseLp_ = 0.0f;
    int burstPos_ = 0;
    uint32_t rng_ = 1;
};
