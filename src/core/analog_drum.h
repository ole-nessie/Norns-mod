#pragma once

#include "voice.h"

// Monophonic analog drum. Bass drum, snare, hat, tom, clap, rim and cowbell,
// in the tradition of the TR-808. The hat's six square waves and the kick's
// pitch drop follow that circuit. Independent implementation; the virtual-
// analog models by Emilie Gillet (Peaks, MIT) were the reference.
class AnalogDrum : public Voice {
public:
    enum Param { MODL, TUNE, DEC, TONE, SNAP, ATTK, ACC, VOL };

    const char* name() const override { return "808"; }
    const ParamInfo* params() const override;
    const char* formatValue(int index, uint8_t value, char* buf) const override;

    void prepare(float sampleRate) override;
    void setParam(int index, uint8_t value) override;
    void noteOn(uint8_t note, float velocity) override;
    void render(float* out, int frames) override;

private:
    void updateDerived();
    void updatePitch();
    float generate();
    float kick();
    float snare();
    float hat();
    float clap();
    float rim();
    float cowbell();
    float noise();
    float bandpass(float x);

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamsPerPage] = {};

    int model_ = 0;
    float tuneSemis_ = 0.0f;
    float tone_ = 0.5f;
    float snap_ = 0.5f;
    float attk_ = 0.5f;
    float accent_ = 1.0f;
    float gain_ = 0.4f;
    float ampCoef_ = 0.0f;
    float pitchCoef_ = 0.0f;
    float clickCoef_ = 0.0f;
    float tickCoef_ = 0.0f;
    float lpCoef_ = 0.2f;
    float hpCoef_ = 0.2f;
    float noiseCoef_ = 0.2f;
    float svfF_ = 0.1f;
    float hz_ = 50.0f;

    bool active_ = false;
    uint8_t note_ = 60;
    float velocity_ = 0.0f;
    float amp_ = 0.0f;
    float pitchEnv_ = 0.0f;
    float click_ = 0.0f;
    float tail_ = 0.0f;
    float phase_ = 0.0f;
    float phase2_ = 0.0f;
    float squares_[6] = {};
    float lp_ = 0.0f;
    float bp_ = 0.0f;
    int clapPos_ = 0;
    uint32_t rng_ = 1;
};
