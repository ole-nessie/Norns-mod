#include "synth.h"

#include <string.h>

#include "controller.h"

Synth::Synth(float sampleRate) : sampleRate_(sampleRate) {
    fxUnit_.prepare(sampleRate_);
    for (int ch = 0; ch < kChannels; ++ch) {
        mix_[ch][0] = 127;
        for (int s = 1; s < kMixSlots; ++s) mix_[ch][s] = 0;
    }
    for (int i = 0; i < kFxParams; ++i) {
        fx_[i] = fxDefault(i);
        fxUnit_.setParam(i, fx_[i]);
    }
}

void Synth::addVoice(int midiChannel, Voice* voice) {
    voice->prepare(sampleRate_);
    VoiceView& view = views_[midiChannel];
    for (int i = 0; i < voice->paramCount(); ++i) view.values_[i] = voice->params()[i].initial;
    lastNote_[midiChannel] = 60;
    __atomic_store_n(&voices_[midiChannel], voice, __ATOMIC_RELAXED);
}

bool Synth::addQueue(MidiQueue* queue) {
    if (queueCount_ == kMaxQueues) return false;
    queues_[queueCount_++] = queue;
    return true;
}

bool Synth::post(const SynthCommand& command) { return commands_.push(command); }

void Synth::markEdited() {
    const uint32_t next = __atomic_load_n(&editSerial_, __ATOMIC_RELAXED) + 1;
    __atomic_store_n(&editSerial_, next, __ATOMIC_RELAXED);
}

int Synth::voicePages(int midiChannel) const {
    const Voice* voice = voiceOnChannel(midiChannel);
    if (!voice) return 0;
    return (voice->paramCount() + kParamsPerPage - 1) / kParamsPerPage;
}

int Synth::pageCount(int midiChannel) const {
    const Voice* voice = voices_[midiChannel];
    if (!voice) return 0;
    return (voice->paramCount() + kParamsPerPage - 1) / kParamsPerPage + 1;
}

void Synth::stepMachine(int delta) {
    if (delta == 0) return;
    static const uint8_t kChoices[] = {kMachineEfmDrum, kMachineSid, kMachineMetal, kMachine808,
                                       kMachineFm,      kMachineFm4, kMachine303,   kMachineChord,
                                       kMachineNone};
    constexpr int kCount = static_cast<int>(sizeof kChoices);
    const int ch = __atomic_load_n(&focusedChannel_, __ATOMIC_RELAXED);
    __atomic_store_n(&editingFx_, 0, __ATOMIC_RELAXED);
    const uint8_t kind = __atomic_load_n(&kind_[ch], __ATOMIC_RELAXED);
    int index = 0;
    for (int i = 0; i < kCount; ++i) {
        if (kChoices[i] == kind) index = i;
    }
    const int steps = delta > 0 ? delta : -delta;
    const int dir = delta > 0 ? 1 : -1;
    for (int n = 0; n < steps; ++n) {
        index += dir;
        if (index < 0) index = kCount - 1;
        if (index >= kCount) index = 0;
    }
    installMachine(ch, kChoices[index]);
    bumpKitLabel(ch);
}

void Synth::addKitDelta(int midiChannel, int delta) {
    if (midiChannel < 0 || midiChannel >= kChannels || delta == 0) return;
    __atomic_fetch_add(&kitDelta_[midiChannel], delta, __ATOMIC_RELAXED);
}

int Synth::takeKitDelta(int midiChannel) {
    if (midiChannel < 0 || midiChannel >= kChannels) return 0;
    return __atomic_exchange_n(&kitDelta_[midiChannel], 0, __ATOMIC_RELAXED);
}

const char* Synth::kitLabel(int midiChannel) const {
    if (midiChannel < 0 || midiChannel >= kChannels) return "";
    if (kitShown_[midiChannel] != __atomic_load_n(&kitGen_[midiChannel], __ATOMIC_RELAXED)) return "";
    return kitLabel_[midiChannel];
}

void Synth::setKitLabel(int midiChannel, const char* name) {
    if (midiChannel < 0 || midiChannel >= kChannels) return;
    char* dst = kitLabel_[midiChannel];
    int i = 0;
    if (name) {
        for (; i < 8 && name[i]; ++i) dst[i] = name[i];
    }
    while (i > 0 && dst[i - 1] == ' ') --i;
    dst[i] = '\0';
    kitShown_[midiChannel] = __atomic_load_n(&kitGen_[midiChannel], __ATOMIC_RELAXED);
}

void Synth::focusChannel(int midiChannel) {
    if (midiChannel < 0 || midiChannel >= kChannels) return;
    __atomic_store_n(&editingFx_, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&focusedChannel_, midiChannel, __ATOMIC_RELAXED);
}

void Synth::focusPage(int page) {
    __atomic_store_n(&editingFx_, 0, __ATOMIC_RELAXED);
    const int ch = __atomic_load_n(&focusedChannel_, __ATOMIC_RELAXED);
    if (page < 0 || page >= pageCount(ch)) return;
    __atomic_store_n(&views_[ch].page_, page, __ATOMIC_RELAXED);
}

void Synth::focusFxPage(int page) {
    if (page < 0 || page >= kFxPages) return;
    __atomic_store_n(&fxPage_, page, __ATOMIC_RELAXED);
    __atomic_store_n(&editingFx_, 1, __ATOMIC_RELAXED);
}

void Synth::bumpKitLabel(int midiChannel) {
    if (midiChannel < 0 || midiChannel >= kChannels) return;
    __atomic_fetch_add(&kitGen_[midiChannel], 1, __ATOMIC_RELAXED);
}

void Synth::touchParam(int midiChannel, int index, uint8_t value) {
    Voice* voice = voices_[midiChannel];
    if (!voice || index < 0 || index >= voice->paramCount()) return;
    voice->setParam(index, value);
    VoiceView& view = views_[midiChannel];
    __atomic_store_n(&view.values_[index], value, __ATOMIC_RELAXED);
    __atomic_store_n(&view.lastTouched_, index, __ATOMIC_RELAXED);
    __atomic_store_n(&view.page_, index / kParamsPerPage, __ATOMIC_RELAXED);
    __atomic_store_n(&focusedChannel_, midiChannel, __ATOMIC_RELAXED);
    bumpKitLabel(midiChannel);
    markEdited();
}

void Synth::writeParam(int midiChannel, int index, uint8_t value) {
    Voice* voice = voices_[midiChannel];
    if (!voice || index < 0 || index >= voice->paramCount()) return;
    voice->setParam(index, value);
    __atomic_store_n(&views_[midiChannel].values_[index], value, __ATOMIC_RELAXED);
    markEdited();
}

void Synth::installMachine(int midiChannel, uint8_t kind) {
    if (midiChannel < 0 || midiChannel >= kChannels || !pool_) return;
    if (__atomic_load_n(&kind_[midiChannel], __ATOMIC_RELAXED) == kind) return;
    Voice* voice = pool_->voice(midiChannel, kind);
    if (kind != kMachineNone && !voice) return;
    if (voice) {
        const int count = voice->paramCount();
        for (int i = 0; i < count; ++i) {
            const uint8_t initial = voice->params()[i].initial;
            voice->setParam(i, initial);
            __atomic_store_n(&views_[midiChannel].values_[i], initial, __ATOMIC_RELAXED);
        }
    }
    __atomic_store_n(&voices_[midiChannel], voice, __ATOMIC_RELAXED);
    __atomic_store_n(&kind_[midiChannel], kind, __ATOMIC_RELAXED);
    __atomic_store_n(&views_[midiChannel].page_, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&views_[midiChannel].lastTouched_, -1, __ATOMIC_RELAXED);
    lastNote_[midiChannel] = 60;
    markEdited();
}

void Synth::moveSlot(int delta) {
    if (delta == 0) return;
    if (__atomic_load_n(&editingFx_, __ATOMIC_RELAXED)) {
        const int page = __atomic_load_n(&fxPage_, __ATOMIC_RELAXED);
        const int count = fxSlots(page);
        const int origin = fxParamIndex(page, 0);
        int slot = __atomic_load_n(&fxTouched_, __ATOMIC_RELAXED) - origin;
        if (slot < 0 || slot >= count) slot = 0;
        slot += delta > 0 ? 1 : -1;
        if (slot < 0) slot = count - 1;
        if (slot >= count) slot = 0;
        __atomic_store_n(&fxTouched_, fxParamIndex(page, slot), __ATOMIC_RELAXED);
        return;
    }
    const int ch = __atomic_load_n(&focusedChannel_, __ATOMIC_RELAXED);
    Voice* voice = voices_[ch];
    if (!voice) return;
    const int voiceCount = voice->paramCount();
    const int mixPage = (voiceCount + kParamsPerPage - 1) / kParamsPerPage;
    if (views_[ch].page_ >= mixPage) {
        int slot = views_[ch].lastTouched_ - mixPage * kParamsPerPage;
        if (slot < 0 || slot >= kMixSlots) slot = 0;
        slot += delta > 0 ? 1 : -1;
        if (slot < 0) slot = kMixSlots - 1;
        if (slot >= kMixSlots) slot = 0;
        __atomic_store_n(&views_[ch].lastTouched_, mixPage * kParamsPerPage + slot, __ATOMIC_RELAXED);
        return;
    }
    const int count = voiceCount;
    int index = views_[ch].lastTouched_;
    if (index < 0 || index >= count) index = views_[ch].page_ * kParamsPerPage;
    const int step = delta > 0 ? 1 : -1;
    for (int n = 0; n < count; ++n) {
        index += step;
        if (index < 0) index = count - 1;
        if (index >= count) index = 0;
        if (voice->params()[index].name) break;
    }
    __atomic_store_n(&views_[ch].lastTouched_, index, __ATOMIC_RELAXED);
    __atomic_store_n(&views_[ch].page_, index / kParamsPerPage, __ATOMIC_RELAXED);
}

void Synth::perform(const SynthCommand& command) {
    switch (command.op) {
        case SynthOp::FocusChannel: focusChannel(command.a); break;
        case SynthOp::PageDelta:
            if (__atomic_load_n(&editingFx_, __ATOMIC_RELAXED)) {
                int page = __atomic_load_n(&fxPage_, __ATOMIC_RELAXED) + command.b;
                if (page < 0) page = 0;
                if (page >= kFxPages) page = kFxPages - 1;
                focusFxPage(page);
            } else {
                focusPage(focusedPage() + command.b);
            }
            break;
        case SynthOp::Nudge: nudgeFocusedParam(command.a, command.b); break;
        case SynthOp::MoveSlot: moveSlot(command.b); break;
        case SynthOp::SetMachine: installMachine(command.a, static_cast<uint8_t>(command.b)); break;
        case SynthOp::SetParam: writeParam(command.a, command.b, command.c); break;
        case SynthOp::SetMix: {
            const int ch = command.a;
            const int slot = command.b;
            if (ch >= 0 && ch < kChannels && slot >= 0 && slot < kMixSlots) {
                __atomic_store_n(&mix_[ch][slot], command.c, __ATOMIC_RELAXED);
                markEdited();
            }
            break;
        }
        case SynthOp::SetFx:
            if (command.a < kFxParams) {
                __atomic_store_n(&fx_[command.a], command.c, __ATOMIC_RELAXED);
                fxUnit_.setParam(command.a, command.c);
                markEdited();
            }
            break;
        case SynthOp::FocusFx:
            __atomic_store_n(&editingFx_, command.a ? 1 : 0, __ATOMIC_RELAXED);
            if (command.a) __atomic_store_n(&fxPage_, 0, __ATOMIC_RELAXED);
            break;
    }
}

void Synth::capture(int midiChannel, KitData& out) const {
    memset(&out, 0, sizeof out);
    if (midiChannel < 0 || midiChannel >= kChannels) return;
    const uint8_t kind = __atomic_load_n(&kind_[midiChannel], __ATOMIC_RELAXED);
    const char* code = machineCode(kind);
    for (int i = 0; code[i] && i < 7; ++i) out.machine[i] = code[i];
    out.count = static_cast<uint8_t>(machineParamCount(kind));
    for (int i = 0; i < out.count; ++i) out.values[i] = views_[midiChannel].value(i);
    for (int s = 0; s < kMixSlots; ++s) out.mix[s] = mixValue(midiChannel, s);
}

void Synth::captureFx(uint8_t* dst) const {
    for (int i = 0; i < kFxParams; ++i) dst[i] = fxValue(i);
}

void Synth::nudgeFocusedParam(int slot, int delta) {
    if (slot < 0 || slot >= kParamsPerPage || delta == 0) return;
    if (__atomic_load_n(&editingFx_, __ATOMIC_RELAXED)) {
        const int page = __atomic_load_n(&fxPage_, __ATOMIC_RELAXED);
        if (slot >= fxSlots(page)) return;
        const int index = fxParamIndex(page, slot);
        int value = __atomic_load_n(&fx_[index], __ATOMIC_RELAXED) + delta;
        if (value < 0) value = 0;
        if (value > 127) value = 127;
        __atomic_store_n(&fx_[index], static_cast<uint8_t>(value), __ATOMIC_RELAXED);
        fxUnit_.setParam(index, static_cast<uint8_t>(value));
        __atomic_store_n(&fxTouched_, index, __ATOMIC_RELAXED);
        markEdited();
        return;
    }
    const int ch = __atomic_load_n(&focusedChannel_, __ATOMIC_RELAXED);
    const Voice* voice = voices_[ch];
    if (!voice) return;
    const int mixPage = (voice->paramCount() + kParamsPerPage - 1) / kParamsPerPage;
    if (views_[ch].page_ >= mixPage) {
        if (slot >= kMixSlots) return;
        const int index = mixStoreIndex(slot);
        int value = __atomic_load_n(&mix_[ch][index], __ATOMIC_RELAXED) + delta;
        if (value < 0) value = 0;
        if (value > 127) value = 127;
        __atomic_store_n(&mix_[ch][index], static_cast<uint8_t>(value), __ATOMIC_RELAXED);
        __atomic_store_n(&views_[ch].lastTouched_, mixPage * kParamsPerPage + slot, __ATOMIC_RELAXED);
        bumpKitLabel(ch);
        markEdited();
        return;
    }
    const int index = views_[ch].page_ * kParamsPerPage + slot;
    if (index >= voice->paramCount() || !voice->params()[index].name) return;
    int value = views_[ch].values_[index] + delta;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    touchParam(ch, index, static_cast<uint8_t>(value));
}

void Synth::audition(bool noteOn) {
    const int ch = __atomic_load_n(&focusedChannel_, __ATOMIC_RELAXED);
    Voice* voice = voices_[ch];
    if (!voice) return;
    const uint8_t note = lastNote_[ch];
    if (noteOn) {
        voice->noteOn(note, 1.0f);
        __atomic_store_n(&views_[ch].triggerCount_, views_[ch].triggerCount_ + 1, __ATOMIC_RELAXED);
    } else {
        voice->noteOff(note);
    }
}

void Synth::panic() {
    for (int ch = 0; ch < kChannels; ++ch) {
        if (voices_[ch]) voices_[ch]->releaseAll();
    }
}

void Synth::handle(const MidiMessage& msg) {
    const uint32_t midi = __atomic_load_n(&midiCount_, __ATOMIC_RELAXED) + 1;
    __atomic_store_n(&midiCount_, midi, __ATOMIC_RELAXED);
    if (applyController(*this, msg)) return;

    const int ch = msg.channel();
    if (ch < 0 || ch >= kChannels) return;
    Voice* voice = voices_[ch];
    if (!voice) return;
    VoiceView& view = views_[ch];

    if (msg.type() == 0x90 && msg.data2 > 0) {
        lastNote_[ch] = msg.data1;
        voice->noteOn(msg.data1, msg.data2 / 127.0f);
        __atomic_store_n(&view.triggerCount_, view.triggerCount_ + 1, __ATOMIC_RELAXED);
    } else if (msg.type() == 0x80 || msg.type() == 0x90) {
        voice->noteOff(msg.data1);
    } else if (msg.type() == 0xB0) {
        // All Sound Off and All Notes Off release held notes on every channel.
        if (msg.data1 == 120 || msg.data1 == 123) {
            panic();
            return;
        }
        const int index = msg.data1 - kFirstParamCc;
        if (index < 0 || index >= voice->paramCount()) return;
        touchParam(ch, index, msg.data2);
    }
}

void Synth::process(float* out, int frames, int channels) {
    // Events are applied at the start of the callback, so timing is quantised
    // to the audio period.
    SynthCommand command;
    while (commands_.pop(command)) perform(command);

    MidiMessage msg;
    for (int q = 0; q < queueCount_; ++q) {
        while (queues_[q]->pop(msg)) handle(msg);
    }

    int done = 0;
    while (done < frames) {
        const int n = frames - done < kBlock ? frames - done : kBlock;
        memset(dry_, 0, sizeof(float) * n);
        memset(sendD_, 0, sizeof(float) * n);
        memset(sendR_, 0, sizeof(float) * n);
        memset(sendC_, 0, sizeof(float) * n);
        memset(sendX_, 0, sizeof(float) * n);
        for (int ch = 0; ch < kChannels; ++ch) {
            Voice* voice = voices_[ch];
            if (!voice) continue;
            memset(tmp_, 0, sizeof(float) * n);
            voice->render(tmp_, n);
            const float vol = __atomic_load_n(&mix_[ch][0], __ATOMIC_RELAXED) / 127.0f;
            const float gain = vol * vol;
            const float dly = gain * (__atomic_load_n(&mix_[ch][1], __ATOMIC_RELAXED) / 127.0f);
            const float rev = gain * (__atomic_load_n(&mix_[ch][2], __ATOMIC_RELAXED) / 127.0f);
            const float cho = gain * (__atomic_load_n(&mix_[ch][3], __ATOMIC_RELAXED) / 127.0f);
            const float drv = gain * (__atomic_load_n(&mix_[ch][4], __ATOMIC_RELAXED) / 127.0f);
            for (int i = 0; i < n; ++i) {
                const float s = tmp_[i];
                dry_[i] += s * gain;
                sendD_[i] += s * dly;
                sendR_[i] += s * rev;
                sendC_[i] += s * cho;
                sendX_[i] += s * drv;
            }
        }
        fxUnit_.process(dry_, sendD_, sendR_, sendC_, sendX_, left_, right_, n);
        for (int i = 0; i < n; ++i) {
            float l = left_[i];
            float r = right_[i];
            l = l > 1.0f ? 1.0f : (l < -1.0f ? -1.0f : l);
            r = r > 1.0f ? 1.0f : (r < -1.0f ? -1.0f : r);
            float* frame = out + (done + i) * channels;
            if (channels <= 1) {
                frame[0] = 0.5f * (l + r);
            } else {
                frame[0] = l;
                frame[1] = r;
                for (int c = 2; c < channels; ++c) frame[c] = 0.5f * (l + r);
            }
        }
        done += n;
    }
}
