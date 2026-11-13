#include "fm_voice.h"

#include <math.h>

namespace {

constexpr float kTwoPi = 6.28318530718f;

const ParamInfo kParams[FmVoice::kParamCount] = {
    {"RATIO", 76}, {"DTUN", 64}, {"IDX", 85},  {"FDBK", 80},
    {"XMOD", 0},   {"TUNE", 64}, {"GLID", 0},  {"VOL", 100},
    {"ATK", 0},    {"DEC", 100}, {"SUS", 0},   {"REL", 80},
    {"FSTR", 115}, {"FATK", 0},  {"FDEC", 80}, {"FEND", 12},
};

const char* const kPageNames[] = {"SYN", "ENV"};

float expMap(uint8_t value, float lo, float hi) {
    return lo * powf(hi / lo, value / 127.0f);
}

// One-pole step. Parameter 0 reaches the target in a single sample.
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

// 0.25 at knob 0, 8 at knob 127. Knob 76 is a ratio of 2.
float modulatorRatio(uint8_t value) { return 0.25f * powf(32.0f, value / 127.0f); }

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

const char* formatRatio(uint8_t value, char* buf) {
    int hundredths = static_cast<int>(modulatorRatio(value) * 100.0f + 0.5f);
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

}  // namespace

const ParamInfo* FmVoice::params() const { return kParams; }

const char* FmVoice::pageName(int page) const {
    return page >= 0 && page < 2 ? kPageNames[page] : "";
}

const char* FmVoice::formatValue(int index, uint8_t value, char* buf) const {
    if (index == RATIO) return formatRatio(value, buf);
    if (index == DTUN) return formatInt((static_cast<int>(value) - 64) * 100 / 64, buf);
    if (index == FDBK) return formatInt(static_cast<int>(value) - 64, buf);
    if (index == TUNE) return formatInt((static_cast<int>(value) - 64) * 24 / 64, buf);
    if (index == GLID) return value == 0 ? "OFF" : nullptr;
    return nullptr;
}

void FmVoice::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamCount; ++i) values_[i] = kParams[i].initial;
    for (int i = 0; i < kVoices; ++i) notes_[i] = Note{};
    rng_ = 0x464d3201u;
    age_ = 0;
    updateDerived();
}

void FmVoice::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamCount) return;
    values_[index] = value;
    updateDerived();
}

void FmVoice::updateDerived() {
    ratio_ = modulatorRatio(values_[RATIO]);
    detune_ = powf(2.0f, ((values_[DTUN] - 64) * (100.0f / 64.0f)) / 1200.0f);
    index_ = values_[IDX] / 127.0f * 6.0f;
    feedback_ = (values_[FDBK] - 64) / 64.0f;
    cross_ = values_[XMOD] / 127.0f * 2.0f;
    tuneSemis_ = (values_[TUNE] - 64) * 24.0f / 64.0f;
    glideCoef_ = values_[GLID] == 0 ? 0.0f : pole(values_[GLID], sampleRate_);
    const float vol = values_[VOL] / 127.0f;
    gain_ = 0.30f * vol * vol;
    sus_ = values_[SUS] / 127.0f;
    fmStart_ = values_[FSTR] / 127.0f;
    fmEnd_ = values_[FEND] / 127.0f;
    atkCoef_ = pole(values_[ATK], sampleRate_);
    decCoef_ = pole(values_[DEC], sampleRate_);
    relCoef_ = pole(values_[REL], sampleRate_);
    fatkCoef_ = pole(values_[FATK], sampleRate_);
    fdecCoef_ = pole(values_[FDEC], sampleRate_);
}

int FmVoice::allocate(uint8_t note) {
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

void FmVoice::trigger(Note& n, uint8_t note, float velocity, bool fresh) {
    const bool slide = !fresh && glideCoef_ != 0.0f;
    const float from = n.pitch;
    n = Note{};
    n.note = note;
    n.velocity = velocity;
    n.pitch = slide ? from : note;
    n.gate = true;
    n.busy = true;
    n.age = ++age_;
    n.ampStage = Attack;
    n.fmStage = Attack;
    n.fm = fmStart_;
    rng_ = rng_ * 1664525u + 1013904223u;
    n.carPhase = (rng_ >> 8) * (1.0f / 16777216.0f);
    rng_ = rng_ * 1664525u + 1013904223u;
    n.modPhase = (rng_ >> 8) * (1.0f / 16777216.0f);
}

void FmVoice::noteOn(uint8_t note, float velocity) {
    const int slot = allocate(note);
    // A stolen voice keeps its pitch so glide can leave the previous note.
    trigger(notes_[slot], note, velocity, !notes_[slot].busy);
}

void FmVoice::noteOff(uint8_t note) {
    for (int i = 0; i < kVoices; ++i) {
        Note& n = notes_[i];
        if (n.busy && n.gate && n.note == note) {
            n.gate = false;
            n.ampStage = Release;
        }
    }
}

void FmVoice::releaseAll() {
    for (int i = 0; i < kVoices; ++i) {
        Note& n = notes_[i];
        if (n.busy && n.gate) {
            n.gate = false;
            n.ampStage = Release;
        }
    }
}

float FmVoice::generate(Note& n) {
    if (n.ampStage == Attack) {
        n.amp += (1.0f - n.amp) * atkCoef_;
        if (n.amp >= 0.999f) {
            n.amp = 1.0f;
            n.ampStage = Decay;
        }
    } else if (n.ampStage == Decay) {
        n.amp += (sus_ - n.amp) * decCoef_;
        if (sus_ >= 0.001f && fabsf(n.amp - sus_) < 0.001f) n.ampStage = Sustain;
    } else if (n.ampStage == Sustain) {
        n.amp = sus_;
    } else {
        n.amp += (0.0f - n.amp) * relCoef_;
    }

    if (n.gate && n.fmStage == Attack) {
        n.fm += (1.0f - n.fm) * fatkCoef_;
        if (n.fm >= 0.999f) {
            n.fm = 1.0f;
            n.fmStage = Decay;
        }
    } else if (n.gate) {
        n.fm += (fmEnd_ - n.fm) * fdecCoef_;
    } else {
        n.fm += (0.0f - n.fm) * relCoef_;
    }

    if (n.amp < 0.0001f && n.ampStage != Attack) {
        n.busy = false;
        n.gate = false;
        return 0.0f;
    }

    if (glideCoef_ == 0.0f) n.pitch = n.note;
    else n.pitch += (n.note - n.pitch) * glideCoef_;

    const float hz = noteHz(n.pitch + tuneSemis_);
    const float invSr = 1.0f / sampleRate_;
    n.carPhase = wrap(n.carPhase + hz * invSr);
    n.modPhase = wrap(n.modPhase + hz * ratio_ * detune_ * invSr);

    // Less feedback as the note rises, so the edge stays similar across the keyboard.
    float comp = 220.0f / hz;
    if (comp > 3.0f) comp = 3.0f;
    if (comp < 0.25f) comp = 0.25f;
    const float bright = 0.35f + 0.65f * n.velocity;
    const float m = sinf(kTwoPi * n.modPhase + n.prev * cross_);
    const float c = sinf(kTwoPi * n.carPhase + m * index_ * n.fm * bright + n.fb * feedback_ * comp);
    n.fb = c;
    n.prev = c;
    return c * n.amp * n.velocity * gain_;
}

void FmVoice::render(float* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int v = 0; v < kVoices; ++v) {
            if (notes_[v].busy) sum += generate(notes_[v]);
        }
        out[i] += sum;
    }
}
