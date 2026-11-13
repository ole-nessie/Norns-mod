#include "efm_drum.h"

#include <math.h>

namespace {

constexpr float kTwoPi = 6.28318530718f;

const ParamInfo kParams[kParamsPerPage] = {
    {"MODL", 0}, {"TUNE", 64}, {"DEC", 72}, {"RAMP", 90},
    {"TONE", 40}, {"NOIS", 24}, {"MOD", 48}, {"VOL", 108},
};

const char* const kModels[] = {"BD", "SD", "LT", "HT", "CH", "OH", "CP", "RS"};

struct Spec {
    float hz;
    float sweep;
    float ratio;
    float body;
    float noise;
    float decay;
    float tone;
    int bursts;
};

const Spec kSpec[] = {
    {46.0f, 5.0f, 1.4f, 1.0f, 0.15f, 1.0f, 180.0f, 0},
    {190.0f, 2.2f, 1.9f, 0.7f, 0.85f, 0.55f, 2200.0f, 0},
    {78.0f, 1.6f, 1.15f, 1.0f, 0.05f, 0.85f, 350.0f, 0},
    {150.0f, 1.3f, 1.3f, 1.0f, 0.06f, 0.7f, 500.0f, 0},
    {340.0f, 0.3f, 6.2f, 0.25f, 1.0f, 0.18f, 7000.0f, 0},
    {300.0f, 0.25f, 5.4f, 0.2f, 1.0f, 0.7f, 5200.0f, 0},
    {240.0f, 0.1f, 0.7f, 0.15f, 1.0f, 0.45f, 1800.0f, 3},
    {720.0f, 0.2f, 2.7f, 0.85f, 0.35f, 0.12f, 2800.0f, 0},
};

int choice(uint8_t value, int count) { return value * count / 128; }

float expMap(uint8_t value, float lo, float hi) {
    return lo * powf(hi / lo, value / 127.0f);
}

float decayCoef(float seconds, float sampleRate) {
    if (seconds < 0.001f) seconds = 0.001f;
    return expf(-6.9078f / (seconds * sampleRate));
}

float poleCoef(float hz, float sampleRate) {
    float coef = 1.0f - expf(-kTwoPi * hz / sampleRate);
    if (coef < 0.0f) coef = 0.0f;
    if (coef > 1.0f) coef = 1.0f;
    return coef;
}

float wrap(float phase) {
    phase -= floorf(phase);
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

}  // namespace

const ParamInfo* EfmDrum::params() const { return kParams; }

const char* EfmDrum::formatValue(int index, uint8_t value, char* /*buf*/) const {
    if (index == MODL) return kModels[choice(value, 8)];
    return nullptr;
}

void EfmDrum::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamsPerPage; ++i) values_[i] = kParams[i].initial;
    rng_ = 0x45464d44u;
    updateDerived();
}

void EfmDrum::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamsPerPage) return;
    values_[index] = value;
    updateDerived();
}

void EfmDrum::updateDerived() {
    model_ = choice(values_[MODL], 8);
    const Spec& s = kSpec[model_];
    baseHz_ = s.hz;
    tuneSemis_ = (values_[TUNE] - 64) * 24.0f / 64.0f;
    sweep_ = (powf(2.0f, values_[RAMP] / 127.0f * s.sweep) - 1.0f);
    modIndex_ = values_[MOD] / 127.0f * 8.0f;
    modRatio_ = s.ratio;
    bodyMix_ = s.body;
    noiseMix_ = s.noise * (values_[NOIS] / 127.0f);
    const float seconds = expMap(values_[DEC], 0.02f, 1.6f) * s.decay;
    ampCoef_ = decayCoef(seconds, sampleRate_);
    pitchCoef_ = decayCoef(0.045f, sampleRate_);
    modCoef_ = decayCoef(seconds * 0.45f, sampleRate_);
    const float tone = s.tone * powf(8.0f, (values_[TONE] - 64) / 64.0f);
    toneCoef_ = poleCoef(tone, sampleRate_);
    bursts_ = s.bursts;
    const float vol = values_[VOL] / 127.0f;
    gain_ = 0.7f * vol * vol;
}

float EfmDrum::noise() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return (rng_ >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

void EfmDrum::noteOn(uint8_t note, float velocity) {
    active_ = true;
    note_ = note;
    velocity_ = velocity;
    ampEnv_ = 1.0f;
    pitchEnv_ = 1.0f;
    modEnv_ = 1.0f;
    carPhase_ = 0.0f;
    modPhase_ = 0.0f;
    noiseLp_ = 0.0f;
    burstPos_ = 0;
}

void EfmDrum::render(float* out, int frames) {
    if (!active_) return;
    const float invSr = 1.0f / sampleRate_;
    const float semi = powf(2.0f, (tuneSemis_ + static_cast<int>(note_) - 60) / 12.0f);
    const int period = static_cast<int>(sampleRate_ * 0.011f);
    const float level = velocity_ * gain_;
    for (int i = 0; i < frames; ++i) {
        const float hz = baseHz_ * semi * (1.0f + sweep_ * pitchEnv_);
        carPhase_ = wrap(carPhase_ + hz * invSr);
        modPhase_ = wrap(modPhase_ + hz * modRatio_ * invSr);
        const float pm = sinf(kTwoPi * modPhase_) * modIndex_ * modEnv_;
        const float body = sinf(kTwoPi * carPhase_ + pm);
        noiseLp_ += toneCoef_ * (noise() - noiseLp_);
        float gate = 1.0f;
        if (bursts_ > 0 && period > 0) {
            const int on = period / 3;
            if (burstPos_ < bursts_ * period) gate = (burstPos_ % period) < on ? 1.0f : 0.12f;
            ++burstPos_;
        }
        const float sig = body * bodyMix_ + noiseLp_ * noiseMix_ * gate;
        out[i] += sig * ampEnv_ * level;
        ampEnv_ *= ampCoef_;
        pitchEnv_ *= pitchCoef_;
        modEnv_ *= modCoef_;
    }
    if (ampEnv_ < 1.0e-4f) active_ = false;
}
