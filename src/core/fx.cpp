#include "fx.h"

#include <math.h>

namespace {

constexpr float kPi = 3.14159265f;

constexpr int kDelayCap = 48000;  // one second at 48 kHz
constexpr int kCombCap = 4096;
constexpr int kApCap = 512;
constexpr int kChorusCap = 1024;

// Fixed storage so the kernel object does not carry these on the stack.
float gDelay[kDelayCap];
float gComb[4][kCombCap];
float gAllpass[4][kApCap];
float gChorus[2][kChorusCap];

const ParamInfo kFxDelay[4] = {
    {"TIME", 96}, {"FDBK", 72}, {"DTNE", 100}, {"DLEV", 90},
};

const ParamInfo kFxReverb[4] = {
    {"SIZE", 80}, {"DECA", 88}, {"RTNE", 64}, {"RLEV", 80},
};

const ParamInfo kFxChorus[3] = {
    {"MODE", 0}, {"DPTH", 64}, {"CLEV", 100},
};

const ParamInfo kFxDrive[4] = {
    {"DRV", 80}, {"SAG", 70}, {"TONE", 96}, {"LEV", 88},
};

// Screen order. Storage stays volume, delay, reverb, chorus, drive.
const ParamInfo kMix[kMixSlots] = {
    {"DLY", 0}, {"REV", 0}, {"CHO", 0}, {"DRV", 0}, {"VOL", 127},
};

const int kMixOrder[kMixSlots] = {1, 2, 3, 4, 0};

const uint8_t kFxInit[kFxParams] = {96, 72, 100, 90, 80, 88, 64, 80, 0, 64, 100, 80, 70, 96, 88};

const char* const kModeNames[] = {"OFF", "I", "II", "I+II"};

// Comb lengths near 30-50 ms. SIZE scales them and they stay inside kCombCap.
const int kCombBase[] = {1557, 1861, 2089, 2377};
const int kApLength[] = {241, 383, 277, 421};

float expMap(uint8_t value, float lo, float hi) {
    return lo * powf(hi / lo, value / 127.0f);
}

float gain2(uint8_t value) {
    const float v = value / 127.0f;
    return v * v;
}

float onePole(float hz, float sampleRate) {
    float c = 1.0f - expf(-2.0f * kPi * hz / sampleRate);
    if (c < 0.0f) c = 0.0f;
    if (c > 1.0f) c = 1.0f;
    return c;
}

void clear(float* data, int n) {
    for (int i = 0; i < n; ++i) data[i] = 0.0f;
}

float readLinear(const float* data, int cap, int write, float delay) {
    float pos = static_cast<float>(write) - delay;
    while (pos < 0.0f) pos += static_cast<float>(cap);
    const int i = static_cast<int>(pos);
    const float frac = pos - static_cast<float>(i);
    const int j = i + 1 < cap ? i + 1 : 0;
    return data[i] + (data[j] - data[i]) * frac;
}

float triangle(float phase) {
    const float t = phase - floorf(phase);
    return t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
}

// Fold the peaks back on themselves, then bend them. The offset makes the two
// sides clip at different levels, so a hot signal goes buzzy rather than merely soft.
float crushSample(float x, float rail) {
    const float bias = 0.22f;
    float a = x / rail + bias;
    for (int n = 0; n < 2; ++n) {
        if (a > 1.0f) a = 2.0f - a;
        else if (a < -1.0f) a = -2.0f - a;
        else break;
    }
    if (a > 1.0f) a = 1.0f;
    else if (a < -1.0f) a = -1.0f;
    const float curved = a - 0.18f * a * a * a;
    return curved * rail;
}

}  // namespace

uint8_t fxDefault(int index) {
    if (index < 0 || index >= kFxParams) return 0;
    return kFxInit[index];
}

const ParamInfo* fxParams(int page) {
    if (page == 1) return kFxReverb;
    if (page == 2) return kFxChorus;
    if (page >= 3) return kFxDrive;
    return kFxDelay;
}

int fxSlots(int page) {
    if (page == 2) return 3;
    return 4;
}

int fxParamIndex(int page, int slot) {
    if (slot < 0) slot = 0;
    if (page == 1) return 4 + slot;
    if (page == 2) return 8 + slot;
    if (page >= 3) return 11 + slot;
    return slot;
}

const char* fxPageName(int page) {
    if (page == 1) return "REV";
    if (page == 2) return "CHO";
    if (page >= 3) return "DRV";
    return "DLY";
}

const ParamInfo* mixParams() { return kMix; }

int mixStoreIndex(int slot) {
    if (slot < 0 || slot >= kMixSlots) return 0;
    return kMixOrder[slot];
}

const char* formatFxValue(int index, uint8_t value, char* buf) {
    if (index == 0) {
        int ms = static_cast<int>(expMap(value, 0.02f, 1.0f) * 1000.0f + 0.5f);
        if (ms > 9999) ms = 9999;
        char* p = buf;
        if (ms >= 1000) *p++ = static_cast<char>('0' + ms / 1000);
        if (ms >= 100) *p++ = static_cast<char>('0' + (ms / 100) % 10);
        if (ms >= 10) *p++ = static_cast<char>('0' + (ms / 10) % 10);
        *p++ = static_cast<char>('0' + ms % 10);
        *p = '\0';
        return buf;
    }
    if (index == 8) return kModeNames[value * 4 / 128];
    return nullptr;
}

void MixFx::prepare(float sampleRate) {
    sampleRate_ = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    clear(gDelay, kDelayCap);
    for (int i = 0; i < 4; ++i) {
        clear(gComb[i], kCombCap);
        clear(gAllpass[i], kApCap);
        combWrite_[i] = 0;
        apWrite_[i] = 0;
        reverbLp_[i] = 0.0f;
        combLength_[i] = kCombBase[i];
    }
    clear(gChorus[0], kChorusCap);
    clear(gChorus[1], kChorusCap);
    delayWrite_ = 0;
    delayLp_ = 0.0f;
    chorusLp_ = 0.0f;
    chorusWrite_ = 0;
    driveLp_ = 0.0f;
    driveDcIn_ = 0.0f;
    driveDcOut_ = 0.0f;
    sagRail_ = 1.0f;
    sagRecover_ = onePole(1.0f, sampleRate_);
    phase1_ = 0.0f;
    phase2_ = 0.25f;
    delayPos_ = expMap(fxDefault(0), 0.02f, 1.0f) * sampleRate_;
    delayTarget_ = delayPos_;
    for (int i = 0; i < kFxParams; ++i) setParam(i, fxDefault(i));
}

void MixFx::setParam(int index, uint8_t value) {
    const float sr = sampleRate_;
    if (index == 0) delayTarget_ = expMap(value, 0.02f, 1.0f) * sr;
    else if (index == 1) delayFeedback_ = value / 127.0f * 0.85f;
    else if (index == 2) delayTone_ = onePole(expMap(value, 400.0f, 14000.0f), sr);
    else if (index == 3) delayLevel_ = gain2(value);
    else if (index == 4) {
        const float scale = 0.55f + 0.9f * (value / 127.0f);
        for (int i = 0; i < 4; ++i) {
            int n = static_cast<int>(kCombBase[i] * scale);
            if (n < 32) n = 32;
            if (n > kCombCap - 2) n = kCombCap - 2;
            combLength_[i] = n;
        }
    } else if (index == 5) {
        reverbFeedback_ = 0.25f + 0.68f * (value / 127.0f);
    } else if (index == 6) {
        reverbDamp_ = onePole(expMap(value, 500.0f, 12000.0f), sr);
    } else if (index == 7) {
        reverbLevel_ = gain2(value) * 0.45f;
    } else if (index == 8) {
        chorusMode_ = value * 4 / 128;
    } else if (index == 9) {
        chorusDepth_ = (value / 64.0f) * 0.00175f * sr;
    } else if (index == 10) {
        chorusLevel_ = gain2(value) * 0.8f;
    } else if (index == 11) {
        driveGain_ = expMap(value, 1.0f, 32.0f);
    } else if (index == 12) {
        const float s = value / 127.0f;
        sagPull_ = s * s * 0.00045f;
    } else if (index == 13) {
        driveTone_ = onePole(expMap(value, 700.0f, 14000.0f), sr);
    } else if (index == 14) {
        driveLevel_ = gain2(value) * 0.65f;
    }
}

void MixFx::process(const float* dry, const float* delaySend, const float* reverbSend,
                    const float* chorusSend, const float* driveSend, float* left, float* right,
                    int frames) {
    const float sr = sampleRate_;
    const float center = 0.00325f * sr;
    const float minDelay = 0.0004f * sr;
    const float maxDelay = static_cast<float>(kChorusCap - 4);
    const float step1 = 0.5f / sr;
    const float step2 = 0.8f / sr;
    const float inputPole = onePole(8000.0f, sr);

    for (int i = 0; i < frames; ++i) {
        delayPos_ += (delayTarget_ - delayPos_) * 0.001f;
        float delay = delayPos_;
        if (delay < 2.0f) delay = 2.0f;
        if (delay > static_cast<float>(kDelayCap - 4)) delay = static_cast<float>(kDelayCap - 4);
        const float echoed = readLinear(gDelay, kDelayCap, delayWrite_, delay);
        delayLp_ += delayTone_ * (echoed - delayLp_);
        if (delayLp_ > -1.0e-12f && delayLp_ < 1.0e-12f) delayLp_ = 0.0f;
        gDelay[delayWrite_] = delaySend[i] + delayLp_ * delayFeedback_;
        if (++delayWrite_ >= kDelayCap) delayWrite_ = 0;

        float comb[4];
        for (int c = 0; c < 4; ++c) {
            int read = combWrite_[c] - combLength_[c];
            if (read < 0) read += kCombCap;
            const float y = gComb[c][read];
            reverbLp_[c] += reverbDamp_ * (y - reverbLp_[c]);
            gComb[c][combWrite_[c]] = reverbSend[i] + reverbLp_[c] * reverbFeedback_;
            combWrite_[c] = (combWrite_[c] + 1) & (kCombCap - 1);
            comb[c] = y;
        }
        float revL = 0.25f * (comb[0] + comb[1] + comb[2] + comb[3]);
        float revR = 0.25f * (comb[0] - comb[1] + comb[2] - comb[3]);
        for (int a = 0; a < 2; ++a) {
            const int len = kApLength[a];
            int read = apWrite_[a] - len;
            if (read < 0) read += kApCap;
            const float z = gAllpass[a][read];
            const float y = z - 0.7f * revL;
            gAllpass[a][apWrite_[a]] = revL + 0.7f * y;
            apWrite_[a] = (apWrite_[a] + 1) & (kApCap - 1);
            revL = y;
        }
        for (int a = 2; a < 4; ++a) {
            const int len = kApLength[a];
            int read = apWrite_[a] - len;
            if (read < 0) read += kApCap;
            const float z = gAllpass[a][read];
            const float y = z - 0.7f * revR;
            gAllpass[a][apWrite_[a]] = revR + 0.7f * y;
            apWrite_[a] = (apWrite_[a] + 1) & (kApCap - 1);
            revR = y;
        }

        phase1_ += step1;
        if (phase1_ >= 1.0f) phase1_ -= 1.0f;
        phase2_ += step2;
        if (phase2_ >= 1.0f) phase2_ -= 1.0f;
        float lfo = 0.0f;
        if (chorusMode_ == 1) lfo = triangle(phase1_);
        else if (chorusMode_ == 2) lfo = triangle(phase2_);
        else if (chorusMode_ == 3) lfo = 0.5f * (triangle(phase1_) + triangle(phase2_));
        chorusLp_ += inputPole * (chorusSend[i] - chorusLp_);
        float dL = center + lfo * chorusDepth_;
        float dR = center - lfo * chorusDepth_;
        if (dL < minDelay) dL = minDelay;
        if (dR < minDelay) dR = minDelay;
        if (dL > maxDelay) dL = maxDelay;
        if (dR > maxDelay) dR = maxDelay;
        const float choL = chorusMode_ == 0 ? 0.0f : readLinear(gChorus[0], kChorusCap, chorusWrite_, dL);
        const float choR = chorusMode_ == 0 ? 0.0f : readLinear(gChorus[1], kChorusCap, chorusWrite_, dR);
        gChorus[0][chorusWrite_] = chorusLp_;
        gChorus[1][chorusWrite_] = chorusLp_;
        if (++chorusWrite_ >= kChorusCap) chorusWrite_ = 0;

        const float crushed = crushSample(driveSend[i] * driveGain_, sagRail_);
        sagRail_ += sagRecover_ * (1.0f - sagRail_);
        sagRail_ -= fabsf(crushed) * sagPull_;
        if (sagRail_ < 0.18f) sagRail_ = 0.18f;
        if (sagRail_ > 1.0f) sagRail_ = 1.0f;
        driveLp_ += driveTone_ * (crushed - driveLp_);
        if (driveLp_ > -1.0e-12f && driveLp_ < 1.0e-12f) driveLp_ = 0.0f;
        const float blocked = driveLp_ - driveDcIn_ + 0.995f * driveDcOut_;
        driveDcIn_ = driveLp_;
        driveDcOut_ = blocked;
        if (driveDcOut_ > -1.0e-12f && driveDcOut_ < 1.0e-12f) driveDcOut_ = 0.0f;
        const float drv = blocked * driveLevel_;

        left[i] = dry[i] + echoed * delayLevel_ + revL * reverbLevel_ + choL * chorusLevel_ + drv;
        right[i] = dry[i] + echoed * delayLevel_ + revR * reverbLevel_ + choR * chorusLevel_ + drv;
    }
}
