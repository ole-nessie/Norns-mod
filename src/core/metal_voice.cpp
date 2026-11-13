#include "metal_voice.h"

#include <math.h>

namespace {

constexpr float kTau = 6.28318530718f;
constexpr float kLevel = 0.45f;

const ParamInfo kParams[kParamsPerPage] = {
    {"MODL", 64}, {"TUNE", 64}, {"COLR", 66}, {"METL", 97},
    {"FDBK", 23}, {"SWEP", 82}, {"DEC", 54},  {"CRSH", 38},
};

const char* const kModels[] = {"FUSE", "STAK", "SPLT", "SHRD", "PIPE", "BOOM"};

int choice(uint8_t value, int count) { return value * count / 128; }

float expMap(uint8_t value, float lo, float hi) {
    return lo * powf(hi / lo, value / 127.0f);
}

float wrapPhase(float x) {
    x -= floorf(x / kTau) * kTau;
    if (x < 0.0f) x += kTau;
    return x;
}

float softClip(float x) {
    return x / (1.0f + fabsf(x));
}

uint32_t xorshift32(uint32_t& state) {
    uint32_t x = state ? state : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

float noiseBipolar(uint32_t& state) {
    return static_cast<float>(xorshift32(state) & 0x00ffffffu) * (2.0f / 16777215.0f) - 1.0f;
}

const char* formatInt(int value, char* buf) {
    char* p = buf;
    if (value < 0) {
        *p++ = '-';
        value = -value;
    }
    if (value >= 100) *p++ = static_cast<char>('0' + value / 100);
    if (value >= 10) *p++ = static_cast<char>('0' + (value / 10) % 10);
    *p++ = static_cast<char>('0' + value % 10);
    *p = '\0';
    return buf;
}

}  // namespace

const ParamInfo* MetalVoice::params() const { return kParams; }

const char* MetalVoice::formatValue(int index, uint8_t value, char* buf) const {
    if (index == MODL) return kModels[choice(value, 6)];
    if (index == TUNE) return formatInt((static_cast<int>(value) - 64) * 24 / 64, buf);
    if (index == SWEP) return formatInt((static_cast<int>(value) - 64) * 100 / 64, buf);
    return nullptr;
}

void MetalVoice::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamsPerPage; ++i) values_[i] = kParams[i].initial;
    rng_ = 0x534f5048u;
    active_ = false;

    // Fixed drive matching a moderate Sophie patch (drive 52%): soft clip,
    // with the hotter curve used above 50%.
    constexpr float drive = 0.52f;
    const float curved = powf((drive - 0.5f) * 2.0f, 1.5f);
    const float units = 50.0f + 250.0f * curved;
    driveGain_ = 1.0f + units * 0.075f;
    driveNorm_ = softClip(driveGain_);
    const float t = (drive - 0.5f) * 2.0f;
    driveNorm_ += (1.0f - driveNorm_) * 0.72f * t;

    updateDerived();
}

void MetalVoice::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamsPerPage) return;
    values_[index] = value;
    updateDerived();
}

void MetalVoice::updateDerived() {
    model_ = choice(values_[MODL], 6);
    tuneSemis_ = (values_[TUNE] - 64) * 24.0f / 64.0f;
    color_ = values_[COLR] / 127.0f;
    metal_ = values_[METL] / 127.0f;
    feedback_ = values_[FDBK] / 127.0f;
    sweep_ = (values_[SWEP] - 64) / 64.0f;
    crush_ = values_[CRSH] / 127.0f;
    ampCoef_ = expf(logf(0.00012f) / (expMap(values_[DEC], 0.03f, 4.0f) * sampleRate_));
    pitchCoef_ = expf(-1.0f / (0.028f * sampleRate_));
    metalCoef_ = expf(-1.0f / (0.135f * sampleRate_));
}

void MetalVoice::noteOn(uint8_t note, float velocity) {
    active_ = true;
    note_ = note;
    velocity_ = velocity;
    ampEnv_ = 1.0f;
    pitchEnv_ = 1.0f;
    metalEnv_ = 1.0f;
    attack_ = 0.0f;
    carrierPhase_ = 0.0f;
    mod1Phase_ = 0.0f;
    mod2Phase_ = noiseBipolar(rng_) * 0.12f;
    mod3Phase_ = noiseBipolar(rng_) * 0.08f;
    feedbackZ_ = 0.0f;
    noiseZ_ = 0.0f;
    crushHold_ = 0.0f;
    crushCount_ = 0;
}

float MetalVoice::generate() {
    ampEnv_ *= ampCoef_;
    pitchEnv_ *= pitchCoef_;
    metalEnv_ *= metalCoef_;
    attack_ += (1.0f - attack_) * 0.085f;
    if (ampEnv_ < 0.00012f) {
        active_ = false;
        return 0.0f;
    }

    const float brightness = model_ >= 4 ? metalEnv_ : pitchEnv_;
    const float sweepOct = sweep_ * 4.5f * pitchEnv_;
    float hz = 440.0f * powf(2.0f, (note_ + tuneSemis_ - 69.0f) / 12.0f + sweepOct);
    if (hz < 18.0f) hz = 18.0f;
    if (hz > 18000.0f) hz = 18000.0f;

    float r1 = 0.47f + color_ * color_ * 7.6f;
    if (model_ == 4) r1 = 1.31f + color_ * 6.0f;
    if (model_ == 5) r1 = 0.60f + color_ * 4.2f;
    const float r2 = 1.31f + color_ * 11.17f;
    const float r3 = 2.07f + (1.0f - color_) * 15.73f;
    const float inc = kTau * hz / sampleRate_;

    carrierPhase_ = wrapPhase(carrierPhase_ + inc);
    mod1Phase_ = wrapPhase(mod1Phase_ + inc * r1);
    mod2Phase_ = wrapPhase(mod2Phase_ + inc * r2);
    mod3Phase_ = wrapPhase(mod3Phase_ + inc * r3);

    const float fb = feedbackZ_ * feedback_ * (2.0f + metal_ * 10.0f);
    const float m1 = sinf(mod1Phase_ + fb);
    const float m2 = sinf(mod2Phase_ + m1 * metal_ * 2.5f);
    const float m3 = sinf(mod3Phase_);
    const float index = metal_ * metal_ * (2.0f + 24.0f * (0.25f + brightness * 0.75f));

    float out = 0.0f;
    if (model_ == 1) {
        out = sinf(carrierPhase_ + index * 0.62f * m1) +
              0.55f * sinf(carrierPhase_ * 0.501f + index * 0.35f * m2) +
              0.32f * sinf(carrierPhase_ * 1.997f + index * 0.22f * m3);
        out *= 0.56f;
    } else if (model_ == 2) {
        out = 0.62f * sinf(carrierPhase_ + index * m1) +
              0.38f * sinf(carrierPhase_ * 1.003f - index * 0.71f * m2) + 0.18f * m3;
    } else if (model_ == 3) {
        const float stair = floorf((carrierPhase_ / kTau) * 32.0f) * (kTau / 32.0f);
        noiseZ_ += (noiseBipolar(rng_) - noiseZ_) * (0.02f + color_ * 0.35f);
        out = sinf(stair + index * 0.55f * m1) * (0.85f - metal_ * 0.25f) + noiseZ_ * metal_ * 0.42f;
    } else if (model_ == 4) {
        const float body = sinf(carrierPhase_ + index * m1);
        const float mix = 0.3125f + metal_ * 0.5f + brightness * 0.125f;
        out = (body * (1.0f - mix) + body * m1 * mix) * 1.25f;
    } else if (model_ == 5) {
        const float body = sinf(carrierPhase_);
        const float edge = sinf(carrierPhase_ + index * m1);
        const float mix = 0.0625f + metal_ * brightness * 0.5f;
        out = body * (1.0f - mix) + edge * mix;
    } else {
        out = sinf(carrierPhase_ + index * (m2 * 0.72f + m1 * 0.28f));
    }

    feedbackZ_ = softClip(out * 1.4f);
    out *= ampEnv_ * attack_;

    if (crush_ >= 0.005f) {
        const int rateDiv = 1 + static_cast<int>(crush_ * crush_ * 31.0f);
        if (crushCount_ <= 0) {
            crushHold_ = out;
            crushCount_ = rateDiv;
        }
        out = crushHold_;
        --crushCount_;
        const float levels = powf(2.0f, 15.0f - crush_ * 12.0f);
        out = floorf(out * levels + 0.5f) / levels;
    }

    return softClip(out * driveGain_) / driveNorm_ * velocity_ * kLevel;
}

void MetalVoice::render(float* out, int frames) {
    if (!active_) return;
    for (int i = 0; i < frames; ++i) {
        if (!active_) return;
        out[i] += generate();
    }
}
