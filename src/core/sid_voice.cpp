#include "sid_voice.h"

#include <math.h>

namespace {

constexpr int kOversample = 4;
constexpr int kBlock = 16;  // samples between pitch and filter coefficient updates
constexpr uint32_t kAccMask = 0xFFFFFF;
constexpr float kPi = 3.14159265359f;

const ParamInfo kParams[SidVoice::kParamCount] = {
    {"PW", 64},   {"PWAD", 0},   {"PWRS", 0},  {"WAVE", 38},
    {"MOD", 0},   {"MSRC", 64},  {"MFRQ", 64}, {"TUNE", 64},
    {"ATK", 0},   {"HOLD", 127}, {"DEC", 70},  {"REL", 40},
    {"DIST", 64}, {"VOL", 100},  {nullptr, 64}, {"PORT", 0},
    {"BASE", 0},  {"WDTH", 127}, {"HPQ", 0},   {"LPQ", 0},
    {"ATK", 0},   {"DEC", 60},   {"BOFS", 64}, {"WOFS", 64},
};

const char* const kPageNames[] = {"SYN", "AMP", "FILT"};
const char* const kWaveNames[] = {"TRI", "SAW", "PULS", "MIX", "NOIS"};
const char* const kModNames[] = {"OFF", "RING", "SYNC", "R+S"};
const char* const kSourceNames[] = {"FIX", "NOTE"};
const char* const kOnOff[] = {"OFF", "ON"};

int choice(uint8_t v, int count) { return v * count / 128; }

float expMap(uint8_t v, float lo, float hi) { return lo * powf(hi / lo, v / 127.0f); }

// Per-sample multiplier that decays to -60 dB in `seconds`.
float decayCoef(float seconds, float sampleRate) {
    return expf(-6.9078f / (seconds * sampleRate));
}

// Linear rise per sample from 0 to 1 in `seconds`; parameter 0 is instant.
float riseInc(uint8_t v, float sampleRate) {
    return v == 0 ? 1.0f : 1.0f / (expMap(v, 0.001f, 5.0f) * sampleRate);
}

// Writes -64..63 for a centred parameter.
const char* formatSigned(uint8_t value, char* buf) {
    int v = value - 64;
    char* p = buf;
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    if (v >= 10) *p++ = static_cast<char>('0' + v / 10);
    *p++ = static_cast<char>('0' + v % 10);
    *p = '\0';
    return buf;
}

float softClip(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

float noteHz(float semitones) { return 440.0f * powf(2.0f, (semitones - 69.0f) / 12.0f); }

}  // namespace

const ParamInfo* SidVoice::params() const { return kParams; }

const char* SidVoice::pageName(int page) const {
    return page >= 0 && page < 3 ? kPageNames[page] : "";
}

const char* SidVoice::formatValue(int index, uint8_t value, char* buf) const {
    switch (index) {
        case PWRS: return kOnOff[choice(value, 2)];
        case WAVE: return kWaveNames[choice(value, 5)];
        case MOD: return kModNames[choice(value, 4)];
        case MSRC: return kSourceNames[choice(value, 2)];
        case HOLD: return value == 127 ? "INF" : nullptr;
        case TUNE:
        case DIST:
        case BOFS:
        case WOFS: return formatSigned(value, buf);
        default: return nullptr;
    }
}

void SidVoice::prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kParamCount; ++i) values_[i] = kParams[i].initial;
    updateDerived();
}

void SidVoice::setParam(int index, uint8_t value) {
    if (index < 0 || index >= kParamCount) return;
    values_[index] = value;
    updateDerived();
}

void SidVoice::updateDerived() {
    const float tickRate = sampleRate_ * kOversample;
    wave_ = choice(values_[WAVE], 5);
    const int mod = choice(values_[MOD], 4);
    ring_ = mod == 1 || mod == 3;
    sync_ = mod == 2 || mod == 3;
    modFollowsNote_ = choice(values_[MSRC], 2) == 1;
    // Sweeps the 12-bit pulse width from about 1/16 to 20 cycles per second.
    pwSweepPerTick_ = values_[PWAD] == 0 ? 0.0f : expMap(values_[PWAD], 256.0f, 80000.0f) / tickRate;

    atkInc_ = riseInc(values_[ATK], sampleRate_);
    holdForever_ = values_[HOLD] == 127;
    holdSamples_ = values_[HOLD] == 0 ? 0 : static_cast<int>(expMap(values_[HOLD], 0.005f, 5.0f) * sampleRate_);
    decCoef_ = decayCoef(expMap(values_[DEC], 0.005f, 10.0f), sampleRate_);
    relCoef_ = decayCoef(expMap(values_[REL], 0.005f, 10.0f), sampleRate_);
    // DIST is gain into the clipping stage: 0 is unity, below 0 adds headroom.
    drive_ = powf(2.0f, (values_[DIST] - 64) / 16.0f);
    const float vol = values_[VOL] / 127.0f;
    gain_ = 0.8f * vol * vol;
    portaCoef_ = values_[PORT] == 0 ? 0.0f : decayCoef(expMap(values_[PORT], 0.01f, 3.0f), sampleRate_ / kBlock);

    fatkInc_ = riseInc(values_[FATK], sampleRate_);
    fdecCoef_ = decayCoef(expMap(values_[FDEC], 0.005f, 10.0f), sampleRate_);
    hpK_ = 1.414f * (1.0f - 0.97f * values_[HPQ] / 127.0f);
    lpK_ = 1.414f * (1.0f - 0.97f * values_[LPQ] / 127.0f);
}

void SidVoice::noteOn(uint8_t note, float velocity) {
    note_ = note;
    velocity_ = velocity;
    if (!hasPlayed_ || portaCoef_ == 0.0f) pitch_ = note;
    hasPlayed_ = true;
    if (choice(values_[PWRS], 2) == 1) pwOffset_ = 0.0f;
    ampStage_ = AmpStage::Attack;
    filterAttack_ = true;
}

void SidVoice::noteOff(uint8_t note) {
    if (note != note_ || ampStage_ == AmpStage::Idle) return;
    ampStage_ = AmpStage::Release;
}

void SidVoice::Svf::set(float hz, float sampleRate, float damping) {
    const float g = tanf(kPi * hz / sampleRate);
    k = damping;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float SidVoice::Svf::process(float x, float& hp) {
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    hp = x - k * v1 - v2;
    return v2;
}

void SidVoice::updateBlock(int frames) {
    const float target = note_;
    if (portaCoef_ == 0.0f) {
        pitch_ = target;
    } else {
        pitch_ = target + (pitch_ - target) * powf(portaCoef_, frames / static_cast<float>(kBlock));
    }
    const float semis = pitch_ + (values_[TUNE] - 64) * (100.0f / 64.0f) / 100.0f;
    const float hz = noteHz(semis);
    const float tickRate = sampleRate_ * kOversample;
    inc_ = static_cast<uint32_t>(hz * 16777216.0f / tickRate);

    const float modHz = modFollowsNote_ ? hz * powf(2.0f, (values_[MFRQ] - 64) / 12.0f)
                                        : expMap(values_[MFRQ], 8.0f, 8000.0f);
    modInc_ = static_cast<uint32_t>(modHz * 16777216.0f / tickRate);

    // MM filter tracking: BASE 0 puts the high-pass two octaves below the note,
    // and BASE and WDTH both move one octave per 8 steps.
    const float baseSteps = values_[BASE] + filterEnv_ * (values_[BOFS] - 64);
    float widthSteps = values_[WDTH] + filterEnv_ * (values_[WOFS] - 64);
    if (widthSteps < 0.0f) widthSteps = 0.0f;
    const float maxHz = 0.45f * sampleRate_;
    float hpHz = 0.25f * hz * powf(2.0f, baseSteps / 8.0f);
    float lpHz = hpHz * powf(2.0f, widthSteps / 8.0f);
    hpHz = hpHz < 10.0f ? 10.0f : (hpHz > maxHz ? maxHz : hpHz);
    lpHz = lpHz < 10.0f ? 10.0f : (lpHz > maxHz ? maxHz : lpHz);
    hp_.set(hpHz, sampleRate_, hpK_);
    lpA_.set(lpHz, sampleRate_, 1.414f);
    lpB_.set(lpHz, sampleRate_, lpK_);
}

float SidVoice::oscillatorSample() {
    int sum = 0;
    for (int t = 0; t < kOversample; ++t) {
        const uint32_t modPrev = modAcc_;
        modAcc_ = (modAcc_ + modInc_) & kAccMask;
        const uint32_t prev = acc_;
        acc_ = (acc_ + inc_) & kAccMask;
        if (sync_ && modAcc_ < modPrev) acc_ = 0;

        if (!(prev & 0x80000) && (acc_ & 0x80000)) {
            const uint32_t bit = ((lfsr_ >> 22) ^ (lfsr_ >> 17)) & 1;
            lfsr_ = ((lfsr_ << 1) | bit) & 0x7FFFFF;
        }

        pwOffset_ += pwSweepPerTick_;
        if (pwOffset_ >= 4096.0f) pwOffset_ -= 4096.0f;
        uint32_t pw = static_cast<uint32_t>(values_[PW] * 32 + pwOffset_) & 0xFFF;
        if (pw == 0) pw = 1;

        const uint32_t saw = acc_ >> 12;
        uint32_t msb = acc_ >> 23;
        if (ring_) msb ^= modAcc_ >> 23;
        const uint32_t tri = ((msb ? ~acc_ : acc_) >> 11) & 0xFFF;
        const uint32_t pulse = saw >= pw ? 0xFFF : 0;

        uint32_t out;
        switch (wave_) {
            case 0: out = tri; break;
            case 1: out = saw; break;
            case 2: out = pulse; break;
            // The SID ANDs the bits of selected waveforms; pulse plus saw.
            case 3: out = saw & pulse; break;
            default:
                out = ((lfsr_ >> 22) & 1) << 11 | ((lfsr_ >> 20) & 1) << 10 |
                      ((lfsr_ >> 16) & 1) << 9 | ((lfsr_ >> 13) & 1) << 8 |
                      ((lfsr_ >> 11) & 1) << 7 | ((lfsr_ >> 7) & 1) << 6 |
                      ((lfsr_ >> 4) & 1) << 5 | ((lfsr_ >> 2) & 1) << 4;
                break;
        }
        sum += static_cast<int>(out);
    }
    return (sum / static_cast<float>(kOversample) - 2048.0f) / 2048.0f;
}

void SidVoice::stepEnvelopes() {
    switch (ampStage_) {
        case AmpStage::Idle: break;
        case AmpStage::Attack:
            ampEnv_ += atkInc_;
            if (ampEnv_ >= 1.0f) {
                ampEnv_ = 1.0f;
                ampStage_ = AmpStage::Hold;
                holdLeft_ = holdSamples_;
            }
            break;
        case AmpStage::Hold:
            if (!holdForever_ && holdLeft_-- <= 0) ampStage_ = AmpStage::Decay;
            break;
        case AmpStage::Decay:
        case AmpStage::Release:
            ampEnv_ *= ampStage_ == AmpStage::Decay ? decCoef_ : relCoef_;
            if (ampEnv_ < 1e-4f) {
                ampEnv_ = 0.0f;
                ampStage_ = AmpStage::Idle;
            }
            break;
    }

    if (filterAttack_) {
        filterEnv_ += fatkInc_;
        if (filterEnv_ >= 1.0f) {
            filterEnv_ = 1.0f;
            filterAttack_ = false;
        }
    } else {
        filterEnv_ *= fdecCoef_;
    }
}

void SidVoice::render(float* out, int frames) {
    if (ampStage_ == AmpStage::Idle) return;
    const float level = gain_ * velocity_;
    for (int start = 0; start < frames; start += kBlock) {
        const int n = frames - start < kBlock ? frames - start : kBlock;
        updateBlock(n);
        for (int i = 0; i < n; ++i) {
            float hp;
            const float osc = oscillatorSample() * drive_;
            hp_.process(osc, hp);
            float unused;
            const float lp = lpB_.process(lpA_.process(hp, unused), unused);
            out[start + i] += softClip(lp) * ampEnv_ * level;
            stepEnvelopes();
        }
        if (ampStage_ == AmpStage::Idle) return;
    }
}
