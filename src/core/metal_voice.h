#pragma once

#include "voice.h"

// Monophonic metallic percussion. Six operator structures (Fuse, Stack,
// Split, Shard, Pipe, Boom), adapted from mestela/schwung-sophie, which
// adapts soejrd/digisophie. Both are MIT; this is one voice and one page,
// with the pitch taken from the MIDI note.
//
// Copyright (c) 2026 Matt Estela
// Copyright (c) 2026 digisophie contributors
// SPDX-License-Identifier: MIT
class MetalVoice : public Voice {
public:
    enum Param { MODL, TUNE, COLR, METL, FDBK, SWEP, DEC, CRSH };

    const char* name() const override { return "METAL"; }
    const ParamInfo* params() const override;
    const char* formatValue(int index, uint8_t value, char* buf) const override;

    void prepare(float sampleRate) override;
    void setParam(int index, uint8_t value) override;
    void noteOn(uint8_t note, float velocity) override;
    void render(float* out, int frames) override;

private:
    void updateDerived();
    float generate();

    float sampleRate_ = 48000.0f;
    uint8_t values_[kParamsPerPage] = {};

    int model_ = 3;
    float tuneSemis_ = 0.0f;
    float color_ = 0.0f;
    float metal_ = 0.0f;
    float feedback_ = 0.0f;
    float sweep_ = 0.0f;
    float crush_ = 0.0f;
    float ampCoef_ = 0.0f;
    float pitchCoef_ = 0.0f;
    float metalCoef_ = 0.0f;
    float driveGain_ = 1.0f;
    float driveNorm_ = 1.0f;

    bool active_ = false;
    uint8_t note_ = 60;
    float velocity_ = 0.0f;
    float ampEnv_ = 0.0f;
    float pitchEnv_ = 0.0f;
    float metalEnv_ = 0.0f;
    float attack_ = 0.0f;
    float carrierPhase_ = 0.0f;
    float mod1Phase_ = 0.0f;
    float mod2Phase_ = 0.0f;
    float mod3Phase_ = 0.0f;
    float feedbackZ_ = 0.0f;
    float noiseZ_ = 0.0f;
    float crushHold_ = 0.0f;
    int crushCount_ = 0;
    uint32_t rng_ = 1;
};
