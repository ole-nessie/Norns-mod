#pragma once

#include <stdint.h>

#include "fx.h"
#include "machines.h"
#include "midi.h"
#include "preset.h"
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
    int page() const { return __atomic_load_n(&page_, __ATOMIC_RELAXED); }
    uint32_t triggerCount() const { return __atomic_load_n(&triggerCount_, __ATOMIC_RELAXED); }

private:
    friend class Synth;
    uint8_t values_[kMaxParams] = {};
    int lastTouched_ = -1;
    int page_ = 0;
    uint32_t triggerCount_ = 0;
};

enum class SynthOp : uint8_t {
    FocusChannel = 1,
    PageDelta,
    Nudge,
    MoveSlot,
    SetMachine,
    SetParam,
    SetMix,
    SetFx,
    FocusFx,
};

struct SynthCommand {
    SynthOp op = SynthOp::FocusChannel;
    uint8_t a = 0;
    int8_t b = 0;
    uint8_t c = 0;
};

class Synth {
public:
    explicit Synth(float sampleRate);

    // Setup, before audio starts. The pool must outlive the synth.
    void setPool(MachinePool* pool) { pool_ = pool; }
    void addVoice(int midiChannel, Voice* voice);
    bool addQueue(MidiQueue* queue);

    // Main-thread requests. Applied on the audio side at the next process().
    bool post(const SynthCommand& command);
    // Before audio starts, apply a command directly.
    void perform(const SynthCommand& command);

    // Audio side: drains the queues, then renders interleaved float frames.
    void process(float* out, int frames, int channels);

    // UI side.
    const Voice* voiceOnChannel(int midiChannel) const {
        return __atomic_load_n(const_cast<Voice**>(&voices_[midiChannel]), __ATOMIC_RELAXED);
    }
    const VoiceView& view(int midiChannel) const { return views_[midiChannel]; }
    int focusedChannel() const { return __atomic_load_n(&focusedChannel_, __ATOMIC_RELAXED); }
    int focusedPage() const { return view(focusedChannel()).page(); }
    bool editingFx() const { return __atomic_load_n(&editingFx_, __ATOMIC_RELAXED) != 0; }
    int fxPage() const { return __atomic_load_n(&fxPage_, __ATOMIC_RELAXED); }
    uint8_t fxValue(int index) const {
        if (index < 0 || index >= kFxParams) return 0;
        return __atomic_load_n(&fx_[index], __ATOMIC_RELAXED);
    }
    int fxTouched() const { return __atomic_load_n(&fxTouched_, __ATOMIC_RELAXED); }
    // Channel strip, stored order: volume, delay, reverb, chorus, drive.
    uint8_t mixValue(int midiChannel, int slot) const {
        if (midiChannel < 0 || midiChannel >= kChannels || slot < 0 || slot >= kMixSlots) return 0;
        return __atomic_load_n(&mix_[midiChannel][slot], __ATOMIC_RELAXED);
    }
    // Pages belonging to the machine itself. The next page is the channel strip.
    int voicePages(int midiChannel) const;
    uint8_t machineOnChannel(int midiChannel) const {
        return __atomic_load_n(&kind_[midiChannel], __ATOMIC_RELAXED);
    }
    uint32_t midiCount() const { return __atomic_load_n(&midiCount_, __ATOMIC_RELAXED); }
    uint32_t editSerial() const { return __atomic_load_n(&editSerial_, __ATOMIC_RELAXED); }
    void capture(int midiChannel, KitData& out) const;
    void captureFx(uint8_t* dst) const;
    // Kit name shown in the header until a knob edit or a machine change.
    const char* kitLabel(int midiChannel) const;
    void setKitLabel(int midiChannel, const char* name);

    // Audio side. Used by the controller (MIDI channel 16) and, later, the panel.
    void focusChannel(int midiChannel);
    void stepMachine(int delta);
    void addKitDelta(int midiChannel, int delta);
    int takeKitDelta(int midiChannel);
    void focusPage(int page);
    void focusFxPage(int page);
    // Adds `delta` to one slot of the focused page and clamps to 0..127.
    void nudgeFocusedParam(int slot, int delta);
    void audition(bool noteOn);
    // Release every held note on every channel.
    void panic();

private:
    friend bool applyController(Synth& synth, const MidiMessage& msg);
    void handle(const MidiMessage& msg);
    void touchParam(int midiChannel, int index, uint8_t value);
    void writeParam(int midiChannel, int index, uint8_t value);
    void installMachine(int midiChannel, uint8_t kind);
    void moveSlot(int delta);
    void bumpKitLabel(int midiChannel);
    void markEdited();
    int pageCount(int midiChannel) const;

    static constexpr int kBlock = 256;

    float sampleRate_;
    MachinePool* pool_ = nullptr;
    SpscQueue<SynthCommand, 256> commands_;
    MidiQueue* queues_[kMaxQueues] = {};
    int queueCount_ = 0;
    Voice* voices_[kMaxVoices] = {};
    VoiceView views_[kMaxVoices];
    int focusedChannel_ = 0;
    uint8_t kind_[kMaxVoices] = {};
    uint32_t midiCount_ = 0;
    uint32_t editSerial_ = 0;
    uint8_t lastNote_[kMaxVoices] = {};
    float dry_[kBlock] = {};
    float sendD_[kBlock] = {};
    float sendR_[kBlock] = {};
    float sendC_[kBlock] = {};
    float sendX_[kBlock] = {};
    float tmp_[kBlock] = {};
    float left_[kBlock] = {};
    float right_[kBlock] = {};

    MixFx fxUnit_;
    uint8_t mix_[kChannels][kMixSlots] = {};
    uint8_t fx_[kFxParams] = {};
    uint8_t editingFx_ = 0;
    int fxPage_ = 0;
    int fxTouched_ = -1;
    int kitDelta_[kChannels] = {};
    uint32_t kitGen_[kChannels] = {};
    uint32_t kitShown_[kChannels] = {};
    char kitLabel_[kChannels][9] = {};
};
