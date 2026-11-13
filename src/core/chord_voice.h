#pragma once

#include "voice.h"

// One incoming note plays a chord. Four chords can overlap. The partials
// are plain oscillators, so this stays separate from the FM machines.
class ChordVoice : public Voice {
public:
    enum Param { TYPE, INV, SPRD, WAVE, DET, ATK, REL, VOL };

    const char* name() const override { return "CHRD"; }
    const ParamInfo* params() const override;
    const char* formatValue(int index, uint8_t value, char* buf) const override;

    void prepare(float sampleRate) override;
    void setParam(int index, uint8_t value) override;
    void noteOn(uint8_t note, float velocity) override;
    void noteOff(uint8_t note) override;
    void releaseAll() override;
    void render(float* out, int frames) override;

private:
    static constexpr int kVoices = 4;
    static constexpr int kPartials = 4;

    struct Note {
        uint8_t root = 0;
        uint8_t count = 0;
        int semi[kPartials] = {};
        float phase[kPartials] = {};
        float phase2[kPartials] = {};
        float amp = 0.0f;
        float velocity = 0.0f;
        uint32_t age = 0;
        bool gate = false;
        bool busy = false;
    };

    void updateDerived();
    int allocate(uint8_t note);
    float oscillator(float phase) const;
    float generate(Note& n);

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamsPerPage] = {};
    Note notes_[kVoices];
    uint32_t age_ = 0;
    uint32_t rng_ = 1;

    int type_ = 2;
    int inversion_ = 0;
    int spread_ = 0;
    int wave_ = 2;
    float detune_ = 1.0f;
    float atkCoef_ = 1.0f;
    float relCoef_ = 0.0f;
    float gain_ = 0.2f;
};
