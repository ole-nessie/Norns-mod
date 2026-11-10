#pragma once

#include <stdint.h>

#include "midi.h"
#include "voice.h"

// One voice per MIDI channel, like one Octatrack MIDI track per drum.
constexpr int kMaxVoices = 16;
constexpr int kMaxQueues = 8;

// CC numbers for the parameters of a voice: page 1 is kFirstParamCc .. +7,
// page 2 is +8 .. +15, page 3 is +16 .. +23. Assign the ones you want in the
// Octatrack MIDI track's CTRL1/CTRL2 SETUP.
constexpr uint8_t kFirstParamCc = 16;

// What the UI may read while audio runs. Written by the audio side only.
class VoiceView {
public:
    uint8_t value(int i) const { return __atomic_load_n(&values_[i], __ATOMIC_RELAXED); }
    int lastTouched() const { return __atomic_load_n(&lastTouched_, __ATOMIC_RELAXED); }
    uint32_t triggerCount() const { return __atomic_load_n(&triggerCount_, __ATOMIC_RELAXED); }

private:
    friend class Synth;
    uint8_t values_[kMaxParams] = {};
    int lastTouched_ = -1;
    uint32_t triggerCount_ = 0;
};

class Synth {
public:
    explicit Synth(float sampleRate) : sampleRate_(sampleRate) {}

    // Setup, before audio starts. Voices are not owned and must outlive the synth.
    void addVoice(int midiChannel, Voice* voice);
    bool addQueue(MidiQueue* queue);

    // Audio side: drains the queues, then renders interleaved float frames.
    void process(float* out, int frames, int channels);

    // UI side.
    const Voice* voiceOnChannel(int midiChannel) const { return voices_[midiChannel]; }
    const VoiceView& view(int midiChannel) const { return views_[midiChannel]; }
    int focusedChannel() const { return __atomic_load_n(&focusedChannel_, __ATOMIC_RELAXED); }

private:
    void handle(const MidiMessage& msg);

    static constexpr int kBlock = 256;

    float sampleRate_;
    MidiQueue* queues_[kMaxQueues] = {};
    int queueCount_ = 0;
    Voice* voices_[kMaxVoices] = {};
    VoiceView views_[kMaxVoices];
    int focusedChannel_ = 0;
    float mono_[kBlock] = {};
};
