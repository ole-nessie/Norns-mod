#pragma once

#include "voice.h"

// Six-voice, four-operator FM. One envelope, four ratios and four levels,
// and four routings. Independent instrument, not a port of a commercial synth.
class Fm4Voice : public Voice {
public:
    enum Param {
        ALGO, FDBK, R1, R2, R3, R4, TUNE, VOL,
        L1, L2, L3, L4, ATK, DEC, SUS, REL,
        kParamCount
    };

    const char* name() const override { return "FM4"; }
    int paramCount() const override { return kParamCount; }
    const ParamInfo* params() const override;
    const char* pageName(int page) const override;
    const char* formatValue(int index, uint8_t value, char* buf) const override;

    void prepare(float sampleRate) override;
    void setParam(int index, uint8_t value) override;
    void noteOn(uint8_t note, float velocity) override;
    void noteOff(uint8_t note) override;
    void releaseAll() override;
    void render(float* out, int frames) override;

private:
    struct Note {
        uint8_t note = 0;
        float velocity = 0.0f;
        float amp = 0.0f;
        float phase[4] = {};
        float fb = 0.0f;
        uint32_t age = 0;
        uint8_t stage = 0;
        bool gate = false;
        bool busy = false;
    };

    enum Stage : uint8_t { Attack, Decay, Sustain, Release };

    void updateDerived();
    int allocate(uint8_t note);
    float generate(Note& n);

    static constexpr int kVoices = 6;

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamCount] = {};
    Note notes_[kVoices];
    uint32_t age_ = 0;
    uint32_t rng_ = 1;

    int algo_ = 0;
    float ratio_[4] = {};
    float level_[4] = {};
    float feedback_ = 0.0f;
    float tuneSemis_ = 0.0f;
    float gain_ = 0.2f;
    float sus_ = 0.0f;
    float atkCoef_ = 1.0f;
    float decCoef_ = 0.0f;
    float relCoef_ = 0.0f;
};
