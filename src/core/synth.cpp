#include "synth.h"

#include <string.h>

void Synth::addVoice(int midiChannel, Voice* voice) {
    voice->prepare(sampleRate_);
    VoiceView& view = views_[midiChannel];
    for (int i = 0; i < voice->paramCount(); ++i) view.values_[i] = voice->params()[i].initial;
    voices_[midiChannel] = voice;
}

bool Synth::addQueue(MidiQueue* queue) {
    if (queueCount_ == kMaxQueues) return false;
    queues_[queueCount_++] = queue;
    return true;
}

void Synth::handle(const MidiMessage& msg) {
    const int ch = msg.channel();
    Voice* voice = voices_[ch];
    if (!voice) return;
    VoiceView& view = views_[ch];

    if (msg.type() == 0x90 && msg.data2 > 0) {
        voice->noteOn(msg.data1, msg.data2 / 127.0f);
        __atomic_store_n(&view.triggerCount_, view.triggerCount_ + 1, __ATOMIC_RELAXED);
        __atomic_store_n(&focusedChannel_, ch, __ATOMIC_RELAXED);
    } else if (msg.type() == 0x80 || msg.type() == 0x90) {
        voice->noteOff(msg.data1);
    } else if (msg.type() == 0xB0) {
        const int index = msg.data1 - kFirstParamCc;
        if (index < 0 || index >= voice->paramCount()) return;
        voice->setParam(index, msg.data2);
        __atomic_store_n(&view.values_[index], msg.data2, __ATOMIC_RELAXED);
        __atomic_store_n(&view.lastTouched_, index, __ATOMIC_RELAXED);
        __atomic_store_n(&focusedChannel_, ch, __ATOMIC_RELAXED);
    }
}

void Synth::process(float* out, int frames, int channels) {
    // Events are applied at the start of the callback, so timing is quantised
    // to the audio period.
    MidiMessage msg;
    for (int q = 0; q < queueCount_; ++q) {
        while (queues_[q]->pop(msg)) handle(msg);
    }

    int done = 0;
    while (done < frames) {
        const int n = frames - done < kBlock ? frames - done : kBlock;
        memset(mono_, 0, sizeof(float) * n);
        for (Voice* voice : voices_) {
            if (voice) voice->render(mono_, n);
        }
        for (int i = 0; i < n; ++i) {
            float s = mono_[i];
            s = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
            float* frame = out + (done + i) * channels;
            for (int c = 0; c < channels; ++c) frame[c] = s;
        }
        done += n;
    }
}
