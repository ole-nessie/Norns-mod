#pragma once

#include "voice.h"

// Six-voice, two-operator FM. A modulator drives the carrier, the carrier
// can feed back into itself, and a second envelope lets that brightness
// start high and settle. Independent instrument; not a port of any
// commercial synth.
class FmVoice : public Voice {
public:
    enum Param {
        RATIO, DTUN, IDX, FDBK, XMOD, TUNE, GLID, VOL,
        ATK, DEC, SUS, REL, FSTR, FATK, FDEC, FEND,
        kParamCount
    };

    const char* name() const override { return "FM"; }
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
        float pitch = 60.0f;
        float amp = 0.0f;
        float fm = 0.0f;
        float carPhase = 0.0f;
        float modPhase = 0.0f;
        float fb = 0.0f;
        float prev = 0.0f;
        uint32_t age = 0;
        uint8_t ampStage = 0;
        uint8_t fmStage = 0;
        bool gate = false;
        bool busy = false;
    };

    enum Stage : uint8_t { Attack, Decay, Sustain, Release };

    void updateDerived();
    int allocate(uint8_t note);
    void trigger(Note& n, uint8_t note, float velocity, bool fresh);
    float generate(Note& n);

    static constexpr int kVoices = 6;

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamCount] = {};
    Note notes_[kVoices];
    uint32_t age_ = 0;
    uint32_t rng_ = 1;

    float ratio_ = 1.0f;
    float detune_ = 1.0f;
    float index_ = 0.0f;
    float feedback_ = 0.0f;
    float cross_ = 0.0f;
    float tuneSemis_ = 0.0f;
    float glideCoef_ = 0.0f;
    float gain_ = 0.2f;
    float sus_ = 0.0f;
    float fmStart_ = 1.0f;
    float fmEnd_ = 0.0f;
    float atkCoef_ = 1.0f;
    float decCoef_ = 0.0f;
    float relCoef_ = 0.0f;
    float fatkCoef_ = 1.0f;
    float fdecCoef_ = 0.0f;
};
