#include "acid_voice.h"

#include <math.h>

namespace {

constexpr float kTwoPi = 6.28318530718f;

const ParamInfo kParams[kParamsPerPage] = {
    {"WAVE", 18}, {"CUT", 48}, {"RESO", 78}, {"ENV", 86},
    {"DEC", 58}, {"ACC", 96}, {"SLID", 0}, {"VOL", 100},
};

float expMap(uint8_t value, float lo, float hi) {
    return lo * powf(hi / lo, value / 127.0f);
}

float pole(uint8_t value, float sampleRate) {
    if (value == 0) return 1.0f;
    const float seconds = expMap(value, 0.004f, 0.28f);
    return 1.0f - expf(-1.0f / (seconds * sampleRate));
}

float decayCoef(uint8_t value, float sampleRate) {
    const float seconds = expMap(value, 0.04f, 1.4f);
    return expf(-6.9078f / (seconds * sampleRate));
}

float noteHz(float semitones) { return 440.0f * powf(2.0f, (semitones - 69.0f) / 12.0f); }

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

const ParamInfo* AcidVoice::params() const { return kParams; }

const char* AcidVoice::formatValue(int index, uint8_t value, char* buf) const {
    if (index == SLID) return value == 0 ? "OFF" : nullptr;
    if (index == WAVE) {
        if (value < 24) return "SAW";
        if (value > 104) return "SQR";
        return formatInt(value, buf);
    }
    return nullptr;
}

void AcidVoice::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamsPerPage; ++i) values_[i] = kParams[i].initial;
    for (int i = 0; i < 4; ++i) ladder_[i] = 0.0f;
    gate_ = false;
    active_ = false;
    env_ = 0.0f;
    updateDerived();
}

void AcidVoice::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamsPerPage) return;
    values_[index] = value;
    updateDerived();
}

void AcidVoice::updateDerived() {
    wave_ = values_[WAVE] / 127.0f;
    cutHz_ = expMap(values_[CUT], 40.0f, 2800.0f);
    reso_ = values_[RESO] / 127.0f;
    envAmt_ = values_[ENVM] / 127.0f;
    accent_ = values_[ACC] / 127.0f;
    slideCoef_ = pole(values_[SLID], sampleRate_);
    decayCoef_ = decayCoef(values_[DEC], sampleRate_);
    const float vol = values_[VOL] / 127.0f;
    gain_ = 0.35f * vol * vol;
}

void AcidVoice::noteOn(uint8_t note, float velocity) {
    const bool legato = gate_ && slideCoef_ < 0.999f;
    gate_ = true;
    active_ = true;
    velocity_ = velocity;
    target_ = note;
    if (!legato) {
        pitch_ = note;
        phase_ = 0.0f;
    }
    env_ = 1.0f;
}

void AcidVoice::noteOff(uint8_t note) {
    if (static_cast<int>(target_ + 0.5f) == note) gate_ = false;
}

void AcidVoice::releaseAll() { gate_ = false; }

void AcidVoice::render(float* out, int frames) {
    if (!active_) return;
    const float octaves = envAmt_ * (2.0f + 4.0f * accent_ * velocity_);
    const float boost = 1.0f + accent_ * velocity_;
    const float res = reso_ * 3.4f;
    for (int i = 0; i < frames; ++i) {
        pitch_ += (target_ - pitch_) * slideCoef_;
        const float hz = noteHz(pitch_);
        phase_ = wrap(phase_ + hz / sampleRate_);
        const float saw = phase_ * 2.0f - 1.0f;
        const float square = phase_ < 0.5f ? 1.0f : -1.0f;
        float osc = saw + (square - saw) * wave_;
        float shaped = tanhf(osc * (1.1f + 0.8f * accent_ * velocity_));

        float cut = cutHz_ * powf(2.0f, env_ * octaves);
        if (cut > sampleRate_ * 0.22f) cut = sampleRate_ * 0.22f;
        float f = 1.0f - expf(-kTwoPi * cut / sampleRate_);
        if (f > 0.42f) f = 0.42f;
        float y = shaped - ladder_[3] * res;
        if (y > 1.4f) y = 1.4f;
        if (y < -1.4f) y = -1.4f;
        for (int s = 0; s < 4; ++s) {
            ladder_[s] += f * (y - ladder_[s]);
            if (ladder_[s] > 2.0f) ladder_[s] = 2.0f;
            if (ladder_[s] < -2.0f) ladder_[s] = -2.0f;
            y = ladder_[s];
        }

        out[i] += y * env_ * boost * gain_;
        env_ *= decayCoef_;
    }
    if (env_ < 1.0e-4f) {
        active_ = false;
        gate_ = false;
    }
}
