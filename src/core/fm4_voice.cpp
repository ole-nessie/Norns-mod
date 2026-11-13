#include "fm4_voice.h"

#include <math.h>

namespace {

constexpr float kTwoPi = 6.28318530718f;

const ParamInfo kParams[Fm4Voice::kParamCount] = {
    {"ALGO", 0}, {"FDBK", 28}, {"R1", 51}, {"R2", 76},
    {"R3", 64}, {"R4", 40}, {"TUNE", 64}, {"VOL", 100},
    {"L1", 110}, {"L2", 72}, {"L3", 48}, {"L4", 36},
    {"ATK", 0}, {"DEC", 80}, {"SUS", 90}, {"REL", 64},
};

const char* const kPageNames[] = {"SYN", "ENV"};
const char* const kAlgos[] = {"STAK", "FORK", "PAIR", "ADD"};

int choice(uint8_t value, int count) { return value * count / 128; }

float expMap(uint8_t value, float lo, float hi) {
    return lo * powf(hi / lo, value / 127.0f);
}

float pole(uint8_t value, float sampleRate) {
    if (value == 0) return 1.0f;
    const float seconds = expMap(value, 0.002f, 8.0f);
    return 1.0f - expf(-1.0f / (seconds * sampleRate));
}

float noteHz(float semitones) { return 440.0f * powf(2.0f, (semitones - 69.0f) / 12.0f); }

float wrap(float phase) {
    phase -= floorf(phase);
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

// 0.25 at knob 0, 8 at knob 127. Knob 51 is a ratio of 1, knob 76 is 2.
float opRatio(uint8_t value) { return 0.25f * powf(32.0f, value / 127.0f); }

const char* formatRatio(uint8_t value, char* buf) {
    int hundredths = static_cast<int>(opRatio(value) * 100.0f + 0.5f);
    if (hundredths > 999) hundredths = 999;
    if (hundredths >= 100) {
        buf[0] = static_cast<char>('0' + hundredths / 100);
        buf[1] = '.';
        buf[2] = static_cast<char>('0' + (hundredths / 10) % 10);
        buf[3] = static_cast<char>('0' + hundredths % 10);
        buf[4] = '\0';
        return buf;
    }
    buf[0] = '0';
    buf[1] = '.';
    buf[2] = static_cast<char>('0' + hundredths / 10);
    buf[3] = static_cast<char>('0' + hundredths % 10);
    buf[4] = '\0';
    return buf;
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

const ParamInfo* Fm4Voice::params() const { return kParams; }

const char* Fm4Voice::pageName(int page) const {
    return page >= 0 && page < 2 ? kPageNames[page] : "";
}

const char* Fm4Voice::formatValue(int index, uint8_t value, char* buf) const {
    if (index == ALGO) return kAlgos[choice(value, 4)];
    if (index >= R1 && index <= R4) return formatRatio(value, buf);
    if (index == TUNE) return formatInt((static_cast<int>(value) - 64) * 24 / 64, buf);
    return nullptr;
}

void Fm4Voice::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamCount; ++i) values_[i] = kParams[i].initial;
    for (int i = 0; i < kVoices; ++i) notes_[i] = Note{};
    rng_ = 0x464d3401u;
    age_ = 0;
    updateDerived();
}

void Fm4Voice::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamCount) return;
    values_[index] = value;
    updateDerived();
}

void Fm4Voice::updateDerived() {
    algo_ = choice(values_[ALGO], 4);
    ratio_[0] = opRatio(values_[R1]);
    ratio_[1] = opRatio(values_[R2]);
    ratio_[2] = opRatio(values_[R3]);
    ratio_[3] = opRatio(values_[R4]);
    for (int i = 0; i < 4; ++i) level_[i] = values_[L1 + i] / 127.0f;
    feedback_ = values_[FDBK] / 127.0f * 1.4f;
    tuneSemis_ = (values_[TUNE] - 64) * 24.0f / 64.0f;
    const float vol = values_[VOL] / 127.0f;
    gain_ = 0.22f * vol * vol;
    sus_ = values_[SUS] / 127.0f;
    atkCoef_ = pole(values_[ATK], sampleRate_);
    decCoef_ = pole(values_[DEC], sampleRate_);
    relCoef_ = pole(values_[REL], sampleRate_);
}

int Fm4Voice::allocate(uint8_t note) {
    int idle = -1;
    int releasing = -1;
    float quietest = 2.0f;
    int oldest = 0;
    for (int i = 0; i < kVoices; ++i) {
        Note& n = notes_[i];
        if (n.busy && n.note == note) return i;
        if (!n.busy) idle = i;
        else if (!n.gate && n.amp < quietest) {
            quietest = n.amp;
            releasing = i;
        }
        if (n.age < notes_[oldest].age) oldest = i;
    }
    if (idle >= 0) return idle;
    if (releasing >= 0) return releasing;
    return oldest;
}

void Fm4Voice::noteOn(uint8_t note, float velocity) {
    Note& n = notes_[allocate(note)];
    n = Note{};
    n.note = note;
    n.velocity = velocity;
    n.gate = true;
    n.busy = true;
    n.stage = Attack;
    n.age = ++age_;
    for (int i = 0; i < 4; ++i) {
        rng_ = rng_ * 1664525u + 1013904223u;
        n.phase[i] = (rng_ >> 8) * (1.0f / 16777216.0f);
    }
}

void Fm4Voice::noteOff(uint8_t note) {
    for (int i = 0; i < kVoices; ++i) {
        Note& n = notes_[i];
        if (n.busy && n.gate && n.note == note) {
            n.gate = false;
            n.stage = Release;
        }
    }
}

void Fm4Voice::releaseAll() {
    for (int i = 0; i < kVoices; ++i) {
        Note& n = notes_[i];
        if (n.busy && n.gate) {
            n.gate = false;
            n.stage = Release;
        }
    }
}

float Fm4Voice::generate(Note& n) {
    if (n.stage == Attack) {
        n.amp += (1.0f - n.amp) * atkCoef_;
        if (n.amp >= 0.999f) {
            n.amp = 1.0f;
            n.stage = Decay;
        }
    } else if (n.stage == Decay) {
        n.amp += (sus_ - n.amp) * decCoef_;
        if (sus_ >= 0.001f && fabsf(n.amp - sus_) < 0.002f) n.stage = Sustain;
    } else if (n.stage == Sustain) {
        n.amp = sus_;
    } else {
        n.amp += (0.0f - n.amp) * relCoef_;
    }
    if (n.amp < 0.0008f && n.stage != Attack) {
        n.busy = false;
        n.gate = false;
        return 0.0f;
    }

    const float hz = noteHz(static_cast<float>(n.note) + tuneSemis_);
    const float invSr = 1.0f / sampleRate_;
    const float bright = 0.45f + 0.55f * n.velocity;
    for (int i = 0; i < 4; ++i) n.phase[i] = wrap(n.phase[i] + hz * ratio_[i] * invSr);

    const float p0 = kTwoPi * n.phase[0];
    const float p1 = kTwoPi * n.phase[1];
    const float p2 = kTwoPi * n.phase[2];
    const float p3 = kTwoPi * n.phase[3] + n.fb * feedback_;
    const float ix1 = level_[1] * 7.0f * bright;
    const float ix2 = level_[2] * 7.0f * bright;
    const float ix3 = level_[3] * 7.0f * bright;
    const float s3 = sinf(p3);
    float y = 0.0f;
    if (algo_ == 0) {
        const float s2 = sinf(p2 + s3 * ix3);
        const float s1 = sinf(p1 + s2 * ix2);
        y = sinf(p0 + s1 * ix1) * level_[0];
    } else if (algo_ == 1) {
        const float s2 = sinf(p2 + s3 * ix3);
        const float s1 = sinf(p1);
        y = sinf(p0 + s2 * ix2 + s1 * ix1) * level_[0];
    } else if (algo_ == 2) {
        const float a = sinf(p1 + s3 * ix3) * level_[1];
        const float b = sinf(p0 + sinf(p2) * ix2) * level_[0];
        y = 0.7f * (a + b);
    } else {
        y = 0.45f * (sinf(p0) * level_[0] + sinf(p1) * level_[1] + sinf(p2) * level_[2] + s3 * level_[3]);
    }
    n.fb = s3;
    return y * n.amp * n.velocity * gain_;
}

void Fm4Voice::render(float* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int v = 0; v < kVoices; ++v) {
            if (notes_[v].busy) sum += generate(notes_[v]);
        }
        out[i] += sum;
    }
}
