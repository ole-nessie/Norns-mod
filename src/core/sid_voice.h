#pragma once

#include "voice.h"

// Monophonic voice after the Monomachine SID-6581 machine: a SID-style
// oscillator (24-bit phase accumulator, 12-bit waveforms, LFSR noise, ring
// modulation and hard sync against a second oscillator), followed by the
// Monomachine track's AHDR amp envelope, distortion and BASE/WDTH filter.
class SidVoice : public Voice {
public:
    enum Param {
        // SYN page
        PW, PWAD, PWRS, WAVE, MOD, MSRC, MFRQ, TUNE,
        // AMP page; slot 14 is the MM's PAN, unused because the output is mono
        ATK, HOLD, DEC, REL, DIST, VOL, PAN, PORT,
        // FILT page
        BASE, WDTH, HPQ, LPQ, FATK, FDEC, BOFS, WOFS,
        kParamCount
    };

    const char* name() const override { return "SID 6581"; }
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
    enum class AmpStage { Idle, Attack, Hold, Decay, Release };

    struct Svf {
        float ic1 = 0.0f, ic2 = 0.0f;
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.414f;
        void set(float hz, float sampleRate, float k);
        // Returns the low-pass output; the high-pass output goes to `hp`.
        float process(float x, float& hp);
    };

    void updateDerived();
    void updateBlock(int frames);
    float oscillatorSample();
    void stepEnvelopes();

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamCount] = {};

    // Derived from parameters.
    int wave_ = 1;
    bool ring_ = false;
    bool sync_ = false;
    bool modFollowsNote_ = true;
    float pwSweepPerTick_ = 0.0f;
    float atkInc_ = 1.0f, decCoef_ = 0.0f, relCoef_ = 0.0f;
    float fatkInc_ = 1.0f, fdecCoef_ = 0.0f;
    int holdSamples_ = 0;
    bool holdForever_ = true;
    float drive_ = 1.0f;
    float gain_ = 0.0f;
    float portaCoef_ = 0.0f;
    float hpK_ = 1.414f, lpK_ = 1.414f;

    // Note state.
    uint8_t note_ = 0;
    bool hasPlayed_ = false;
    float velocity_ = 0.0f;
    float pitch_ = 48.0f;  // glided, in semitones

    // Oscillators, per oversampled tick.
    uint32_t acc_ = 0, modAcc_ = 0;
    uint32_t inc_ = 0, modInc_ = 0;
    uint32_t lfsr_ = 0x7FFFF8;
    float pwOffset_ = 0.0f;  // pulse sweep position in 12-bit units

    AmpStage ampStage_ = AmpStage::Idle;
    float ampEnv_ = 0.0f;
    int holdLeft_ = 0;
    bool filterAttack_ = false;
    float filterEnv_ = 0.0f;

    Svf hp_, lpA_, lpB_;
};
