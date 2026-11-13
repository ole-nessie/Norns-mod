#pragma once

#include <stdint.h>

#include "panel_event.h"
#include "synth.h"

class Canvas;
class Storage;

// Play screen controls plus the save/load menu. File I/O stays on the caller
// thread; sound changes are posted to the synth.
class Console {
public:
    Console(Synth& synth, Storage* storage) : synth_(synth), storage_(storage) {}

    // Call after the startup machines (and any loaded state) are in place,
    // before audio starts. Loads the newest valid autosave.
    void loadStartup();
    // Remember the edit counter so startup itself is not written back.
    void arm(uint32_t nowMs);

    bool open() const { return mode_ != Mode::Play; }
    void handle(const PanelEvent& event, uint32_t nowMs);
    void tick(uint32_t nowMs);

    // Draws the menu when it is open and returns true. The play page draws
    // itself when this returns false.
    bool update(Canvas& canvas, uint32_t nowMs);

private:
    enum class Mode : uint8_t { Play, Menu, SetSave, SetLoad, KitSave, KitLoad, Machine, Notice };

    void send(const SynthCommand& command);
    void applySet(const SetData& set);
    void applyKit(int channel, const KitData& kit);
    bool readSet(int slot, SetData& set);
    bool readKit(int slot, KitData& kit);
    bool writeSet(int slot, const SetData& set, uint32_t sequence);
    bool writeKit(int slot, const KitData& kit, uint32_t sequence);
    bool recallKit(int slot, KitData& kit);
    // Kit load lists only kits for that channel's machine. An empty channel sees every kit.
    bool kitVisible(int channel, int slot, KitData* out);
    int firstKit();
    int nextKit(int channel, int slot, int direction);
    void applyEncoderKits();
    void saveState();
    void captureSet(SetData& set) const;
    void enterSlot(Mode mode);
    void confirm(uint32_t nowMs);
    void refreshPreview();
    void loadSlotName();
    void copyEditedName(char* dst, int cap) const;
    void notify(const char* text, uint32_t nowMs);
    void draw(Canvas& canvas);
    void drawKeys(Canvas& canvas);

    Synth& synth_;
    Storage* storage_;
    Mode mode_ = Mode::Play;
    Mode back_ = Mode::Menu;
    int menuIndex_ = 0;
    int slot_ = 1;
    int machineIndex_ = 0;
    int namePos_ = 0;
    char name_[9] = {};
    char preview_[12] = {};
    char notice_[16] = {};
    uint32_t noticeUntilMs_ = 0;
    uint32_t seenSerial_ = 0;
    uint32_t lastChangeMs_ = 0;
    uint32_t lastWriteMs_ = 0;
    uint32_t stateSequence_ = 0;
    bool pending_ = false;
    bool drawn_ = false;
    int kitSlot_[kChannels] = {};
    uint8_t kitKind_[kChannels] = {};
};
