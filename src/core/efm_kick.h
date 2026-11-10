#pragma once

#include "voice.h"

// Bass drum in the spirit of the MachineDrum EFM BD: a sine carrier with an
// exponential pitch sweep, phase-modulated by a decaying sine modulator.
// This is an independent implementation, not a port of Elektron code.
class EfmKick : public Voice {
public:
    enum Param { PTCH, DEC, RAMP, RDEC, MOD, MFRQ, MDEC, VOL };

    const char* name() const override { return "EFM BD"; }
    const ParamInfo* params() const override;

    void prepare(float sampleRate) override;
    void setParam(int index, uint8_t value) override;
    void noteOn(uint8_t note, float velocity) override;
    void render(float* out, int frames) override;

private:
    void updateDerived();

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamsPerPage] = {};

    float baseHz_ = 50.0f;
    float sweep_ = 0.0f;  // extra pitch at the start of the sweep, as a ratio of baseHz_
    float modIndex_ = 0.0f;
    float modRatio_ = 1.0f;
    float gain_ = 0.6f;
    float ampCoef_ = 0.0f;
    float pitchCoef_ = 0.0f;
    float modCoef_ = 0.0f;

    bool active_ = false;
    float velocity_ = 0.0f;
    float ampEnv_ = 0.0f;
    float pitchEnv_ = 0.0f;
    float modEnv_ = 0.0f;
    float carrierPhase_ = 0.0f;
    float modPhase_ = 0.0f;
};
