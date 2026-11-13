#include "chord_voice.h"

#include <math.h>

namespace {

constexpr float kTwoPi = 6.28318530718f;

const ParamInfo kParams[kParamsPerPage] = {
    {"TYPE", 32}, {"INV", 0}, {"SPRD", 0}, {"WAVE", 64},
    {"DET", 18}, {"ATK", 0}, {"REL", 48}, {"VOL", 96},
};

const char* const kTypes[] = {"OCT", "5TH", "MAJ", "MIN", "SUS", "MA7", "MI7", "DOM"};
const char* const kWaves[] = {"SIN", "TRI", "SAW", "SQR"};

struct Shape {
    int count;
    int semi[4];
};

const Shape kShapes[] = {
    {2, {0, 12, 0, 0}}, {2, {0, 7, 0, 0}},  {3, {0, 4, 7, 0}}, {3, {0, 3, 7, 0}},
    {3, {0, 5, 7, 0}},  {4, {0, 4, 7, 11}}, {4, {0, 3, 7, 10}}, {4, {0, 4, 7, 10}},
};

int choice(uint8_t value, int count) { return value * count / 128; }

float expMap(uint8_t value, float lo, float hi) {
    return lo * powf(hi / lo, value / 127.0f);
}

float pole(uint8_t value, float sampleRate) {
    if (value == 0) return 1.0f;
    const float seconds = expMap(value, 0.002f, 4.0f);
    return 1.0f - expf(-1.0f / (seconds * sampleRate));
}

float decayCoef(uint8_t value, float sampleRate) {
    const float seconds = expMap(value, 0.015f, 6.0f);
    return expf(-4.0f / (seconds * sampleRate));
}

float noteHz(int semitones) { return 440.0f * powf(2.0f, (semitones - 69) / 12.0f); }

float wrap(float phase) {
    phase -= floorf(phase);
    if (phase < 0.0f) phase += 1.0f;
    return phase;
}

}  // namespace

const ParamInfo* ChordVoice::params() const { return kParams; }

const char* ChordVoice::formatValue(int index, uint8_t value, char* /*buf*/) const {
    if (index == TYPE) return kTypes[choice(value, 8)];
    if (index == WAVE) return kWaves[choice(value, 4)];
    if (index == DET || index == SPRD) return value == 0 ? "OFF" : nullptr;
    return nullptr;
}

void ChordVoice::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamsPerPage; ++i) values_[i] = kParams[i].initial;
    for (int i = 0; i < kVoices; ++i) notes_[i] = Note{};
    rng_ = 0x43485244u;
    age_ = 0;
    updateDerived();
}

void ChordVoice::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamsPerPage) return;
    values_[index] = value;
    updateDerived();
}

void ChordVoice::updateDerived() {
    type_ = choice(values_[TYPE], 8);
    inversion_ = choice(values_[INV], 4);
    spread_ = choice(values_[SPRD], 3);
    wave_ = choice(values_[WAVE], 4);
    const float cents = values_[DET] / 127.0f * 25.0f;
    detune_ = powf(2.0f, cents / 1200.0f);
    atkCoef_ = pole(values_[ATK], sampleRate_);
    relCoef_ = decayCoef(values_[REL], sampleRate_);
    const float vol = values_[VOL] / 127.0f;
    gain_ = 0.28f * vol * vol;
}

int ChordVoice::allocate(uint8_t note) {
    int idle = -1;
    int oldest = 0;
    for (int i = 0; i < kVoices; ++i) {
        Note& n = notes_[i];
        if (n.busy && n.root == note && n.gate) return i;
        if (!n.busy) idle = i;
        if (n.age < notes_[oldest].age) oldest = i;
    }
    return idle >= 0 ? idle : oldest;
}

void ChordVoice::noteOn(uint8_t note, float velocity) {
    Note& n = notes_[allocate(note)];
    n = Note{};
    n.root = note;
    n.velocity = velocity;
    n.gate = true;
    n.busy = true;
    n.age = ++age_;
    Shape shape = kShapes[type_];
    n.count = static_cast<uint8_t>(shape.count);
    for (int i = 0; i < shape.count; ++i) n.semi[i] = shape.semi[i];
    for (int r = 0; r < inversion_ && r < shape.count; ++r) {
        const int first = n.semi[0];
        for (int i = 0; i < shape.count - 1; ++i) n.semi[i] = n.semi[i + 1];
        n.semi[shape.count - 1] = first + 12;
    }
    for (int i = 1; i < shape.count; ++i) n.semi[i] += i * spread_ * 12;
    for (int i = 0; i < shape.count; ++i) {
        rng_ = rng_ * 1664525u + 1013904223u;
        n.phase[i] = (rng_ >> 8) * (1.0f / 16777216.0f);
        n.phase2[i] = n.phase[i];
    }
}

void ChordVoice::noteOff(uint8_t note) {
    for (int i = 0; i < kVoices; ++i) {
        Note& n = notes_[i];
        if (n.busy && n.gate && n.root == note) n.gate = false;
    }
}

void ChordVoice::releaseAll() {
    for (int i = 0; i < kVoices; ++i) {
        if (notes_[i].busy && notes_[i].gate) notes_[i].gate = false;
    }
}

float ChordVoice::oscillator(float phase) const {
    if (wave_ == 0) return sinf(kTwoPi * phase);
    if (wave_ == 1) {
        if (phase < 0.5f) return phase * 4.0f - 1.0f;
        return 3.0f - phase * 4.0f;
    }
    if (wave_ == 2) return phase * 2.0f - 1.0f;
    return phase < 0.5f ? 1.0f : -1.0f;
}

float ChordVoice::generate(Note& n) {
    if (n.gate) n.amp += (1.0f - n.amp) * atkCoef_;
    else n.amp *= relCoef_;
    if (!n.gate && n.amp < 0.0008f) {
        n.busy = false;
        return 0.0f;
    }
    float sum = 0.0f;
    const float invSr = 1.0f / sampleRate_;
    const bool second = detune_ > 1.0001f;
    for (int i = 0; i < n.count; ++i) {
        const float hz = noteHz(static_cast<int>(n.root) + n.semi[i]);
        n.phase[i] = wrap(n.phase[i] + hz * invSr);
        float s = oscillator(n.phase[i]);
        if (second) {
            n.phase2[i] = wrap(n.phase2[i] + hz * detune_ * invSr);
            s = 0.5f * (s + oscillator(n.phase2[i]));
        }
        sum += s;
    }
    const float norm = n.count > 0 ? 1.0f / static_cast<float>(n.count) : 0.0f;
    return sum * norm * n.amp * n.velocity * gain_;
}

void ChordVoice::render(float* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int v = 0; v < kVoices; ++v) {
            if (notes_[v].busy) sum += generate(notes_[v]);
        }
        out[i] += sum;
    }
}
