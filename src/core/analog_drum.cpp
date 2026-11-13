#include "analog_drum.h"

#include <math.h>

namespace {

constexpr float kPi = 3.14159265359f;
constexpr float kTwoPi = 6.28318530718f;

const ParamInfo kParams[kParamsPerPage] = {
    {"MODL", 8}, {"TUNE", 64}, {"DEC", 80}, {"TONE", 48},
    {"SNAP", 96}, {"ATTK", 72}, {"ACC", 110}, {"VOL", 108},
};

const char* const kModels[] = {"BD", "SD", "HAT", "TOM", "CLAP", "RIM", "BELL"};
const float kBaseHz[] = {52.0f, 180.0f, 2800.0f, 110.0f, 1200.0f, 420.0f, 540.0f};
const float kHatRatio[] = {1.0f, 1.304f, 1.466f, 1.787f, 1.932f, 2.536f};

int choice(uint8_t value, int count) { return value * count / 128; }

float expMap(uint8_t value, float lo, float hi) {
    return lo * powf(hi / lo, value / 127.0f);
}

float decayCoef(float seconds, float sampleRate) {
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

const char* formatInt(int value, char* buf) {
    char* p = buf;
    if (value < 0) {
        *p++ = '-';
        value = -value;
    }
    if (value >= 10) *p++ = static_cast<char>('0' + value / 10);
    *p++ = static_cast<char>('0' + value % 10);
    *p = '\0';
    return buf;
}

}  // namespace

const ParamInfo* AnalogDrum::params() const { return kParams; }

const char* AnalogDrum::formatValue(int index, uint8_t value, char* buf) const {
    if (index == MODL) return kModels[choice(value, 7)];
    if (index == TUNE) return formatInt((static_cast<int>(value) - 64) * 24 / 64, buf);
    return nullptr;
}

void AnalogDrum::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamsPerPage; ++i) values_[i] = kParams[i].initial;
    rng_ = 0x38303801u;
    active_ = false;
    for (int i = 0; i < 6; ++i) squares_[i] = 0.17f * i;
    updateDerived();
}

void AnalogDrum::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamsPerPage) return;
    values_[index] = value;
    updateDerived();
}

void AnalogDrum::updateDerived() {
    model_ = choice(values_[MODL], 7);
    tuneSemis_ = (values_[TUNE] - 64) * 24.0f / 64.0f;
    tone_ = values_[TONE] / 127.0f;
    snap_ = values_[SNAP] / 127.0f;
    attk_ = values_[ATTK] / 127.0f;
    accent_ = 0.35f + 0.65f * values_[ACC] / 127.0f;
    const float vol = values_[VOL] / 127.0f;
    gain_ = 0.55f * vol * vol;
    const float seconds = expMap(values_[DEC], 0.012f, 1.6f);
    ampCoef_ = decayCoef(seconds, sampleRate_);
    pitchCoef_ = decayCoef(0.012f, sampleRate_);
    clickCoef_ = decayCoef(0.0014f, sampleRate_);
    tickCoef_ = decayCoef(0.0025f, sampleRate_);
    lpCoef_ = poleCoef(180.0f * powf(36.0f, tone_), sampleRate_);
    hpCoef_ = poleCoef(700.0f * powf(18.0f, tone_), sampleRate_);
    noiseCoef_ = poleCoef(500.0f * powf(8.0f, tone_), sampleRate_);
    const float bpHz = 450.0f * powf(10.0f, tone_);
    svfF_ = 2.0f * sinf(kPi * bpHz / sampleRate_);
    if (svfF_ > 0.85f) svfF_ = 0.85f;
    updatePitch();
}

void AnalogDrum::updatePitch() {
    const float semi = tuneSemis_ + (static_cast<int>(note_) - 60);
    float hz = kBaseHz[model_] * powf(2.0f, semi / 12.0f);
    if (hz < 20.0f) hz = 20.0f;
    if (hz > 12000.0f) hz = 12000.0f;
    hz_ = hz;
}

float AnalogDrum::noise() {
    uint32_t x = rng_ ? rng_ : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return static_cast<float>(x & 0x00ffffffu) * (2.0f / 16777215.0f) - 1.0f;
}

float AnalogDrum::bandpass(float x) {
    lp_ += svfF_ * bp_;
    const float high = x - lp_ - 0.55f * bp_;
    bp_ += svfF_ * high;
    if (lp_ > 3.0f) lp_ = 3.0f;
    if (lp_ < -3.0f) lp_ = -3.0f;
    if (bp_ > 3.0f) bp_ = 3.0f;
    if (bp_ < -3.0f) bp_ = -3.0f;
    return bp_;
}

void AnalogDrum::noteOn(uint8_t note, float velocity) {
    note_ = note;
    velocity_ = velocity;
    active_ = true;
    amp_ = 1.0f;
    pitchEnv_ = 1.0f;
    click_ = 1.0f;
    tail_ = 0.0f;
    clapPos_ = 0;
    phase_ = 0.0f;
    phase2_ = 0.0f;
    lp_ = 0.0f;
    bp_ = 0.0f;
    updatePitch();
}

float AnalogDrum::kick() {
    pitchEnv_ *= pitchCoef_;
    amp_ *= ampCoef_;
    click_ *= clickCoef_;
    const float hz = hz_ * (1.0f + snap_ * 5.5f * pitchEnv_);
    phase_ = wrap(phase_ + hz / sampleRate_);
    const float body = sinf(kTwoPi * phase_);
    const float click = noise() * click_ * attk_;
    const float x = body * amp_ + click * 0.85f;
    lp_ += (x - lp_) * lpCoef_;
    return lp_ * 1.15f;
}

float AnalogDrum::snare() {
    amp_ *= ampCoef_;
    click_ *= clickCoef_;
    phase_ = wrap(phase_ + hz_ / sampleRate_);
    phase2_ = wrap(phase2_ + hz_ * 1.84f / sampleRate_);
    const float body = sinf(kTwoPi * phase_) + 0.55f * sinf(kTwoPi * phase2_);
    const float n = noise();
    lp_ += (n - lp_) * noiseCoef_;
    const float air = (n - lp_) * amp_ * (0.15f + 0.95f * snap_);
    const float click = n * click_ * attk_ * 0.7f;
    const float x = body * amp_ * (1.05f - 0.45f * snap_) + air + click;
    return x * 0.72f;
}

float AnalogDrum::hat() {
    amp_ *= ampCoef_;
    float squares = 0.0f;
    for (int i = 0; i < 6; ++i) {
        squares_[i] = wrap(squares_[i] + hz_ * kHatRatio[i] / sampleRate_);
        squares += squares_[i] < 0.5f ? 1.0f : -1.0f;
    }
    squares *= 0.16f;
    const float n = noise();
    const float x = squares * (1.0f - snap_ * 0.7f) + n * snap_ * 0.65f;
    lp_ += (x - lp_) * hpCoef_;
    return (x - lp_) * amp_ * (0.55f + 0.45f * attk_) * 2.4f;
}

float AnalogDrum::clap() {
    const float rate = sampleRate_ / 48000.0f;
    const int starts[] = {0, static_cast<int>(480 * rate), static_cast<int>(1056 * rate),
                          static_cast<int>(1680 * rate)};
    const int burstLen = static_cast<int>(220 * rate);
    float burst = 0.0f;
    for (int start : starts) {
        const int age = clapPos_ - start;
        if (age >= 0 && age < burstLen) {
            const float a = 1.0f - static_cast<float>(age) / burstLen;
            if (a > burst) burst = a;
        }
    }
    if (clapPos_ > static_cast<int>(2000 * rate)) {
        if (tail_ == 0.0f) tail_ = 1.0f;
        tail_ *= ampCoef_;
    }
    ++clapPos_;
    const float env = burst * (1.05f - 0.35f * snap_) + tail_ * (0.3f + 0.85f * snap_);
    amp_ = env;
    return bandpass(noise()) * env * (0.7f + 0.5f * attk_) * 1.4f;
}

float AnalogDrum::rim() {
    amp_ *= ampCoef_;
    click_ *= tickCoef_;
    phase_ = wrap(phase_ + hz_ / sampleRate_);
    const float body = sinf(kTwoPi * phase_) * amp_ * (1.0f - 0.35f * snap_);
    const float tick = noise() * click_ * (0.45f + attk_);
    const float x = body + tick * (0.4f + snap_);
    lp_ += (x - lp_) * lpCoef_;
    return lp_ * 0.9f;
}

float AnalogDrum::cowbell() {
    amp_ *= ampCoef_;
    phase_ = wrap(phase_ + hz_ / sampleRate_);
    phase2_ = wrap(phase2_ + hz_ * (1.47f + snap_ * 0.18f) / sampleRate_);
    const float a = phase_ < 0.5f ? 1.0f : -1.0f;
    const float b = phase2_ < 0.5f ? 1.0f : -1.0f;
    const float x = a * 0.62f + b * 0.38f;
    return bandpass(x) * amp_ * (0.65f + 0.4f * attk_) * 0.85f;
}

float AnalogDrum::generate() {
    float x = 0.0f;
    if (model_ == 1) x = snare();
    else if (model_ == 2) x = hat();
    else if (model_ == 4) x = clap();
    else if (model_ == 5) x = rim();
    else if (model_ == 6) x = cowbell();
    else x = kick();

    const bool clapDone = model_ == 4 && clapPos_ > static_cast<int>(0.25f * sampleRate_) && tail_ < 0.00008f;
    const bool envDone = model_ != 4 && amp_ < 0.00008f;
    if (clapDone || envDone) {
        active_ = false;
        return 0.0f;
    }
    return x * velocity_ * accent_ * gain_;
}

void AnalogDrum::render(float* out, int frames) {
    if (!active_) return;
    for (int i = 0; i < frames; ++i) {
        if (!active_) return;
        out[i] += generate();
    }
}
