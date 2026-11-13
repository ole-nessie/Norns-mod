#pragma once

#include "voice.h"

// Monophonic acid bass. Saw or square into a resonant four-pole, with the
// accent opening the filter and a legato slide when the previous note is
// still held. Original voice; the panel name is 303.
class AcidVoice : public Voice {
public:
    enum Param { WAVE, CUT, RESO, ENVM, DEC, ACC, SLID, VOL };

    const char* name() const override { return "303"; }
    const ParamInfo* params() const override;
    const char* formatValue(int index, uint8_t value, char* buf) const override;

    void prepare(float sampleRate) override;
    void setParam(int index, uint8_t value) override;
    void noteOn(uint8_t note, float velocity) override;
    void noteOff(uint8_t note) override;
    void releaseAll() override;
    void render(float* out, int frames) override;

private:
    void updateDerived();

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamsPerPage] = {};

    float wave_ = 0.0f;
    float cutHz_ = 200.0f;
    float reso_ = 0.0f;
    float envAmt_ = 0.0f;
    float accent_ = 0.0f;
    float slideCoef_ = 1.0f;
    float decayCoef_ = 0.0f;
    float gain_ = 0.4f;

    bool gate_ = false;
    bool active_ = false;
    float pitch_ = 48.0f;
    float target_ = 48.0f;
    float velocity_ = 0.0f;
    float env_ = 0.0f;
    float phase_ = 0.0f;
    float ladder_[4] = {};
};
