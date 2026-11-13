#pragma once

#include <stdint.h>

#include "voice.h"

// Master effects. One delay, one reverb, one chorus and one drive for the whole mix.
// Each machine feeds them from its MIX page. The lines live in static
// storage; prepare() only clears them.

constexpr int kFxParams = 16;
constexpr int kFxPages = 4;
constexpr int kMixSlots = 5;

// Stored order stays TIME FDBK DTNE DLEV / SIZE DECA RTNE RLEV / MODE DPTH CLEV / DRV SAG TONE LEV.
// The pages are delay (0-3), reverb (4-7), chorus (8-10) and drive (11-14).
uint8_t fxDefault(int index);
const ParamInfo* fxParams(int page);
int fxSlots(int page);
int fxParamIndex(int page, int slot);
const char* fxPageName(int page);
const char* formatFxValue(int index, uint8_t value, char* buf);

const ParamInfo* mixParams();
// Screen order is delay, reverb, chorus, drive, then volume on the first lower encoder.
// Stored order is volume first, so a kit saved before the drive send still lines up.
int mixStoreIndex(int slot);

class MixFx {
public:
    void prepare(float sampleRate);
    void setParam(int index, uint8_t value);

    // dry is the channel sum. The other four are post-fader sends.
    // left and right may alias none of the inputs.
    void process(const float* dry, const float* delaySend, const float* reverbSend,
                 const float* chorusSend, const float* driveSend, float* left, float* right,
                 int frames);

private:
    float sampleRate_ = 48000.0f;
    float delayTarget_ = 1000.0f;
    float delayPos_ = 1000.0f;
    float delayFeedback_ = 0.0f;
    float delayTone_ = 1.0f;
    float delayLevel_ = 0.0f;
    float delayLp_ = 0.0f;
    int delayWrite_ = 0;

    float reverbFeedback_ = 0.5f;
    float reverbDamp_ = 0.5f;
    float reverbLevel_ = 0.0f;
    float reverbLp_[4] = {};
    int combLength_[4] = {};
    int combWrite_[4] = {};
    int apWrite_[4] = {};

    float chorusDepth_ = 0.0f;
    float chorusLevel_ = 0.0f;
    float chorusLp_ = 0.0f;
    float driveGain_ = 1.0f;
    float driveTone_ = 1.0f;
    float driveLevel_ = 0.0f;
    float driveLp_ = 0.0f;
    float sagPull_ = 0.0f;
    float sagRecover_ = 0.0001f;
    float sagRail_ = 1.0f;
    float driveDcIn_ = 0.0f;
    float driveDcOut_ = 0.0f;
    float phase1_ = 0.0f;
    float phase2_ = 0.0f;
    int chorusMode_ = 0;
    int chorusWrite_ = 0;
};
