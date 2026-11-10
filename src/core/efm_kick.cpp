#include "efm_kick.h"

#include <math.h>

namespace {

constexpr float kTwoPi = 6.28318530718f;

const ParamInfo kParams[kParamsPerPage] = {
    {"PTCH", 40}, {"DEC", 72}, {"RAMP", 70}, {"RDEC", 30},
    {"MOD", 0},   {"MFRQ", 40}, {"MDEC", 30}, {"VOL", 100},
};

// Maps 0..127 exponentially onto [lo, hi].
float expMap(uint8_t v, float lo, float hi) {
    return lo * powf(hi / lo, v / 127.0f);
}

// Per-sample multiplier that decays to -60 dB in `seconds`.
float decayCoef(float seconds, float sampleRate) {
    return expf(-6.9078f / (seconds * sampleRate));
}

float wrap(float phase) {
    return phase >= 1.0f ? phase - 1.0f : phase;
}

}  // namespace

const ParamInfo* EfmKick::params() const { return kParams; }

void EfmKick::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamsPerPage; ++i) values_[i] = kParams[i].initial;
    updateDerived();
}

void EfmKick::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamsPerPage) return;
    values_[index] = value;
    updateDerived();
}

void EfmKick::updateDerived() {
    baseHz_ = expMap(values_[PTCH], 30.0f, 160.0f);
    ampCoef_ = decayCoef(expMap(values_[DEC], 0.03f, 3.0f), sampleRate_);
    sweep_ = powf(2.0f, values_[RAMP] / 127.0f * 48.0f / 12.0f) - 1.0f;
    pitchCoef_ = decayCoef(expMap(values_[RDEC], 0.003f, 0.4f), sampleRate_);
    modIndex_ = values_[MOD] / 127.0f * 6.0f;
    modRatio_ = expMap(values_[MFRQ], 0.25f, 8.0f);
    modCoef_ = decayCoef(expMap(values_[MDEC], 0.005f, 2.0f), sampleRate_);
    const float vol = values_[VOL] / 127.0f;
    gain_ = vol * vol;
}

void EfmKick::noteOn(uint8_t, float velocity) {
    active_ = true;
    velocity_ = velocity;
    ampEnv_ = 1.0f;
    pitchEnv_ = 1.0f;
    modEnv_ = 1.0f;
    carrierPhase_ = 0.0f;
    modPhase_ = 0.0f;
}

void EfmKick::render(float* out, int frames) {
    if (!active_) return;
    const float invSr = 1.0f / sampleRate_;
    const float level = velocity_ * gain_;
    for (int i = 0; i < frames; ++i) {
        const float hz = baseHz_ * (1.0f + sweep_ * pitchEnv_);
        carrierPhase_ = wrap(carrierPhase_ + hz * invSr);
        modPhase_ = wrap(modPhase_ + hz * modRatio_ * invSr);
        const float pm = modIndex_ * modEnv_ * sinf(kTwoPi * modPhase_);
        out[i] += sinf(kTwoPi * carrierPhase_ + pm) * ampEnv_ * level;
        ampEnv_ *= ampCoef_;
        pitchEnv_ *= pitchCoef_;
        modEnv_ *= modCoef_;
    }
    if (ampEnv_ < 1e-5f) active_ = false;
}
