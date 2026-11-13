#include "ui/console.h"

#include <string.h>

#include "machines.h"
#include "preset.h"
#include "storage.h"
#include "synth.h"
#include "ui/canvas.h"

namespace {

constexpr int kMenuCount = 6;
const char* const kMenu[kMenuCount] = {"SET SAVE", "SET LOAD", "KIT SAVE", "KIT LOAD", "MACHINE", "FX"};
const uint8_t kMachineChoices[] = {kMachineEfmDrum, kMachineSid, kMachineMetal, kMachine808,
                                   kMachineFm,      kMachineFm4, kMachine303,   kMachineChord,
                                   kMachineNone};
constexpr uint32_t kQuietMs = 3000;
constexpr uint32_t kForceMs = 30000;
constexpr uint32_t kNoticeMs = 900;
constexpr int kNameLen = 8;
const char kGlyphs[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";

void twoDigits(int n, char* out) {
    if (n < 0) n = 0;
    if (n > 99) n = 99;
    out[0] = static_cast<char>('0' + n / 10);
    out[1] = static_cast<char>('0' + n % 10);
    out[2] = '\0';
}

void copyText(char* dst, int cap, const char* src) {
    int i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

void fileName(char* out, const char* prefix, int slot) {
    char digits[3];
    twoDigits(slot, digits);
    int n = 0;
    for (int i = 0; prefix[i]; ++i) out[n++] = prefix[i];
    out[n++] = '_';
    out[n++] = digits[0];
    out[n++] = digits[1];
    const char* ext = ".bin";
    for (int i = 0; ext[i]; ++i) out[n++] = ext[i];
    out[n] = '\0';
}

}  // namespace

void Console::send(const SynthCommand& command) { synth_.post(command); }

void Console::loadStartup() {
    if (!storage_ || !storage_->ready()) return;
    uint8_t buf[kPresetBytes];
    SetData best;
    uint32_t bestSeq = 0;
    bool found = false;
    const char* names[] = {"state_a.bin", "state_b.bin"};
    for (const char* name : names) {
        int len = 0;
        SetData set;
        uint32_t seq = 0;
        if (!storage_->read(name, buf, kPresetBytes, &len)) continue;
        if (!decodeSet(buf, len, set, seq)) continue;
        if (!found || seq >= bestSeq) {
            best = set;
            bestSeq = seq;
            found = true;
        }
    }
    if (!found) return;
    for (int ch = 0; ch < kChannels; ++ch) {
        const KitData& kit = best.channels[ch];
        synth_.perform({SynthOp::SetMachine, static_cast<uint8_t>(ch),
                        static_cast<int8_t>(machineKind(kit.machine)), 0});
        for (int i = 0; i < kit.count && i < kMaxParams; ++i) {
            synth_.perform({SynthOp::SetParam, static_cast<uint8_t>(ch), static_cast<int8_t>(i),
                            kit.values[i]});
        }
        for (int s = 0; s < kMixSlots; ++s) {
            synth_.perform({SynthOp::SetMix, static_cast<uint8_t>(ch), static_cast<int8_t>(s), kit.mix[s]});
        }
    }
    for (int i = 0; i < kFxParams; ++i) {
        synth_.perform({SynthOp::SetFx, static_cast<uint8_t>(i), 0, best.fx[i]});
    }
    stateSequence_ = bestSeq;
}

void Console::arm(uint32_t nowMs) {
    seenSerial_ = synth_.editSerial();
    lastChangeMs_ = nowMs;
    lastWriteMs_ = nowMs;
    pending_ = false;
}

void Console::applySet(const SetData& set) {
    for (int ch = 0; ch < kChannels; ++ch) applyKit(ch, set.channels[ch]);
    for (int i = 0; i < kFxParams; ++i) send({SynthOp::SetFx, static_cast<uint8_t>(i), 0, set.fx[i]});
    send({SynthOp::FocusChannel, 0, 0, 0});
}

void Console::applyKit(int channel, const KitData& kit) {
    send({SynthOp::SetMachine, static_cast<uint8_t>(channel),
          static_cast<int8_t>(machineKind(kit.machine)), 0});
    for (int i = 0; i < kit.count && i < kMaxParams; ++i) {
        send({SynthOp::SetParam, static_cast<uint8_t>(channel), static_cast<int8_t>(i), kit.values[i]});
    }
    for (int s = 0; s < kMixSlots; ++s) {
        send({SynthOp::SetMix, static_cast<uint8_t>(channel), static_cast<int8_t>(s), kit.mix[s]});
    }
}

void Console::captureSet(SetData& set) const {
    memset(&set, 0, sizeof set);
    memcpy(set.name, "STATE", 5);
    for (int ch = 0; ch < kChannels; ++ch) synth_.capture(ch, set.channels[ch]);
    synth_.captureFx(set.fx);
}

bool Console::readSet(int slot, SetData& set) {
    if (!storage_ || !storage_->ready()) return false;
    char name[20];
    fileName(name, "set", slot);
    uint8_t buf[kPresetBytes];
    int len = 0;
    uint32_t seq = 0;
    return storage_->read(name, buf, kPresetBytes, &len) && decodeSet(buf, len, set, seq);
}

bool Console::readKit(int slot, KitData& kit) {
    if (!storage_ || !storage_->ready()) return false;
    char name[20];
    fileName(name, "kit", slot);
    uint8_t buf[kPresetBytes];
    int len = 0;
    uint32_t seq = 0;
    return storage_->read(name, buf, kPresetBytes, &len) && decodeKit(buf, len, kit, seq);
}

bool Console::recallKit(int slot, KitData& kit) {
    return readKit(slot, kit) || factoryKit(slot, kit);
}

bool Console::kitVisible(int channel, int slot, KitData* out) {
    KitData kit;
    if (channel < 0 || channel >= kChannels) return false;
    if (!recallKit(slot, kit) || !kit.machine[0]) return false;
    const uint8_t kind = synth_.machineOnChannel(channel);
    if (kind != kMachineNone && machineKind(kit.machine) != kind) return false;
    if (out) *out = kit;
    return true;
}

int Console::firstKit() {
    const int channel = synth_.focusedChannel();
    for (int slot = 1; slot <= kKitSlots; ++slot) {
        if (kitVisible(channel, slot, nullptr)) return slot;
    }
    return 0;
}

int Console::nextKit(int channel, int slot, int direction) {
    const int step = direction > 0 ? 1 : -1;
    for (int i = 0; i < kKitSlots; ++i) {
        slot += step;
        if (slot > kKitSlots) slot = 1;
        if (slot < 1) slot = kKitSlots;
        if (kitVisible(channel, slot, nullptr)) return slot;
    }
    return 0;
}

bool Console::writeSet(int slot, const SetData& set, uint32_t sequence) {
    if (!storage_ || !storage_->ready()) return false;
    char name[20];
    fileName(name, "set", slot);
    uint8_t buf[kPresetBytes];
    const int n = encodeSet(set, sequence, buf, kPresetBytes);
    return n > 0 && storage_->write(name, buf, n);
}

bool Console::writeKit(int slot, const KitData& kit, uint32_t sequence) {
    if (!storage_ || !storage_->ready()) return false;
    char name[20];
    fileName(name, "kit", slot);
    uint8_t buf[kPresetBytes];
    const int n = encodeKit(kit, sequence, buf, kPresetBytes);
    return n > 0 && storage_->write(name, buf, n);
}

void Console::saveState() {
    if (!storage_ || !storage_->ready()) return;
    SetData set;
    captureSet(set);
    uint8_t buf[kPresetBytes];
    const uint32_t seq = stateSequence_ + 1;
    const int n = encodeSet(set, seq, buf, kPresetBytes);
    if (n <= 0) return;
    const char* name = (seq & 1u) ? "state_a.bin" : "state_b.bin";
    if (!storage_->write(name, buf, n)) return;
    stateSequence_ = seq;
}

void Console::refreshPreview() {
    preview_[0] = '\0';
    if (mode_ == Mode::SetLoad) {
        SetData set;
        copyText(preview_, sizeof preview_, readSet(slot_, set) ? set.name : "EMPTY");
    } else if (mode_ == Mode::KitLoad) {
        KitData kit;
        if (kitVisible(synth_.focusedChannel(), slot_, &kit)) {
            const char* label = (kit.name[0] && kit.name[0] != ' ') ? kit.name : machineName(machineKind(kit.machine));
            copyText(preview_, sizeof preview_, label);
        } else {
            copyText(preview_, sizeof preview_, "EMPTY");
        }
    }
}

void Console::loadSlotName() {
    namePos_ = 0;
    for (int i = 0; i < kNameLen; ++i) name_[i] = ' ';
    name_[kNameLen] = '\0';
    const char* found = nullptr;
    if (mode_ == Mode::SetSave) {
        SetData set;
        if (readSet(slot_, set) && set.name[0]) found = set.name;
    } else if (mode_ == Mode::KitSave) {
        KitData kit;
        if (readKit(slot_, kit) && kit.name[0] && kit.name[0] != ' ') found = kit.name;
    } else {
        return;
    }
    char fallback[9];
    if (!found) {
        char digits[3];
        twoDigits(slot_, digits);
        const char* prefix = mode_ == Mode::KitSave ? "KIT " : "SET ";
        int n = 0;
        for (; prefix[n]; ++n) fallback[n] = prefix[n];
        fallback[n++] = digits[0];
        fallback[n++] = digits[1];
        fallback[n] = '\0';
        found = fallback;
    }
    for (int i = 0; i < kNameLen && found[i]; ++i) name_[i] = found[i];
}

void Console::copyEditedName(char* dst, int cap) const {
    memset(dst, 0, cap);
    int end = kNameLen;
    while (end > 0 && name_[end - 1] == ' ') --end;
    if (end == 0) return;
    for (int i = 0; i < end && i < cap - 1; ++i) dst[i] = name_[i];
}

void Console::notify(const char* text, uint32_t nowMs) {
    int i = 0;
    for (; text[i] && i < 15; ++i) notice_[i] = text[i];
    notice_[i] = '\0';
    noticeUntilMs_ = nowMs + kNoticeMs;
    back_ = mode_;
    mode_ = Mode::Notice;
    drawn_ = false;
}

void Console::enterSlot(Mode mode) {
    mode_ = mode;
    slot_ = 1;
    if (mode == Mode::KitLoad) {
        const int first = firstKit();
        if (first > 0) slot_ = first;
    }
    refreshPreview();
    loadSlotName();
    drawn_ = false;
}

void Console::confirm(uint32_t nowMs) {
    if (mode_ == Mode::Machine) {
        const int ch = synth_.focusedChannel();
        send({SynthOp::SetMachine, static_cast<uint8_t>(ch),
              static_cast<int8_t>(kMachineChoices[machineIndex_]), 0});
        synth_.setKitLabel(ch, "");
        kitSlot_[ch] = 0;
        kitKind_[ch] = kMachineChoices[machineIndex_];
        notify("MACHINE", nowMs);
        return;
    }
    if (mode_ == Mode::KitLoad) {
        KitData kit;
        memset(&kit, 0, sizeof kit);
        const uint8_t kind = synth_.machineOnChannel(synth_.focusedChannel());
        const bool found = recallKit(slot_, kit) && kit.machine[0];
        const bool same = found && (kind == kMachineNone || machineKind(kit.machine) == kind);
        if (!found || !same) {
            notify(!storage_ || !storage_->ready() ? "NO SD" : "EMPTY", nowMs);
            return;
        }
        const int ch = synth_.focusedChannel();
        applyKit(ch, kit);
        synth_.setKitLabel(ch, kit.name);
        kitSlot_[ch] = slot_;
        kitKind_[ch] = machineKind(kit.machine);
        notify("LOADED KIT", nowMs);
        return;
    }
    if (!storage_ || !storage_->ready()) {
        notify("NO SD", nowMs);
        return;
    }
    char digits[3];
    twoDigits(slot_, digits);
    if (mode_ == Mode::SetSave || mode_ == Mode::SetLoad) {
        SetData set;
        if (mode_ == Mode::SetSave) {
            captureSet(set);
            copyEditedName(set.name, sizeof set.name);
            if (!set.name[0]) {
                char digits[3];
                twoDigits(slot_, digits);
                memcpy(set.name, "SET ", 4);
                set.name[4] = digits[0];
                set.name[5] = digits[1];
            }
            if (!writeSet(slot_, set, 1)) {
                notify("FAILED", nowMs);
                return;
            }
            char saved[] = "SAVED SET 00";
            saved[10] = digits[0];
            saved[11] = digits[1];
            notify(saved, nowMs);
            return;
        }
        if (!readSet(slot_, set)) {
            notify("EMPTY", nowMs);
            return;
        }
        applySet(set);
        notify("LOADED SET", nowMs);
        return;
    }
    if (mode_ == Mode::KitSave) {
        KitData kit;
        const int ch = synth_.focusedChannel();
        synth_.capture(ch, kit);
        copyEditedName(kit.name, sizeof kit.name);
        if (!kit.name[0]) {
            char digits[3];
            twoDigits(slot_, digits);
            memcpy(kit.name, "KIT ", 4);
            kit.name[4] = digits[0];
            kit.name[5] = digits[1];
        }
        if (!writeKit(slot_, kit, 1)) {
            notify("FAILED", nowMs);
            return;
        }
        notify("SAVED KIT", nowMs);
    }
}

void Console::handle(const PanelEvent& event, uint32_t nowMs) {
    if (mode_ == Mode::Notice) return;
    const bool key = event.control == PanelControl::Key && event.delta > 0;
    const bool encoder = event.control == PanelControl::Encoder && event.delta != 0;
    if (!key && !encoder) return;

    if (mode_ == Mode::Play) {
        if (key && event.index == 0) {
            mode_ = Mode::Menu;
            drawn_ = false;
        } else if (key && event.index == 1) {
            send({SynthOp::MoveSlot, 0, -1, 0});
        } else if (key && event.index == 2) {
            send({SynthOp::MoveSlot, 0, 1, 0});
        } else if (encoder && event.index == 0) {
            int ch = synth_.focusedChannel() + (event.delta > 0 ? 1 : -1);
            if (ch < 0) ch = kChannels - 1;
            if (ch >= kChannels) ch = 0;
            send({SynthOp::FocusChannel, static_cast<uint8_t>(ch), 0, 0});
        } else if (encoder && event.index == 1) {
            send({SynthOp::PageDelta, 0, event.delta, 0});
        } else if (encoder && event.index == 2) {
            const int page = synth_.focusedPage();
            int slot = synth_.view(synth_.focusedChannel()).lastTouched() - page * kParamsPerPage;
            if (slot < 0 || slot >= kParamsPerPage) slot = 0;
            send({SynthOp::Nudge, static_cast<uint8_t>(slot), event.delta, 0});
        }
        return;
    }

    if (key && event.index == 0) {
        mode_ = Mode::Play;
        drawn_ = false;
        return;
    }
    if (mode_ == Mode::Menu) {
        if (encoder && event.index == 1) {
            menuIndex_ = (menuIndex_ + (event.delta > 0 ? 1 : kMenuCount - 1)) % kMenuCount;
            drawn_ = false;
        } else if (key && event.index == 1) {
            mode_ = Mode::Play;
            drawn_ = false;
        } else if (key && event.index == 2) {
            if (menuIndex_ == 0) enterSlot(Mode::SetSave);
            else if (menuIndex_ == 1) enterSlot(Mode::SetLoad);
            else if (menuIndex_ == 2) enterSlot(Mode::KitSave);
            else if (menuIndex_ == 3) enterSlot(Mode::KitLoad);
            else if (menuIndex_ == 5) {
                send({SynthOp::FocusFx, 1, 0, 0});
                mode_ = Mode::Play;
                drawn_ = false;
            } else {
                mode_ = Mode::Machine;
                const uint8_t kind = synth_.machineOnChannel(synth_.focusedChannel());
                machineIndex_ = 0;
                for (int i = 0; i < static_cast<int>(sizeof kMachineChoices); ++i) {
                    if (kMachineChoices[i] == kind) machineIndex_ = i;
                }
                drawn_ = false;
            }
        }
        return;
    }

    if (key && event.index == 1) {
        mode_ = Mode::Menu;
        drawn_ = false;
        return;
    }

    const bool naming = mode_ == Mode::SetSave || mode_ == Mode::KitSave;
    const int limit = (mode_ == Mode::KitSave || mode_ == Mode::KitLoad) ? kKitSlots : kSetSlots;
    if (encoder && event.index == 0 && mode_ != Mode::Machine) {
        if (mode_ == Mode::KitLoad) {
            const int steps = event.delta > 0 ? event.delta : -event.delta;
            const int direction = event.delta > 0 ? 1 : -1;
            for (int i = 0; i < steps; ++i) {
                const int next = nextKit(synth_.focusedChannel(), slot_, direction);
                if (next <= 0) break;
                slot_ = next;
            }
        } else {
            slot_ += event.delta;
            while (slot_ < 1) slot_ += limit;
            while (slot_ > limit) slot_ -= limit;
        }
        refreshPreview();
        loadSlotName();
        drawn_ = false;
        return;
    }
    if (naming && encoder && event.index == 1) {
        namePos_ = (namePos_ + (event.delta > 0 ? 1 : kNameLen - 1)) % kNameLen;
        drawn_ = false;
        return;
    }
    if (naming && encoder && event.index == 2) {
        int count = 0;
        while (kGlyphs[count]) ++count;
        int index = 0;
        while (kGlyphs[index] && kGlyphs[index] != name_[namePos_]) ++index;
        if (index == count) index = 0;
        index = (index + event.delta) % count;
        if (index < 0) index += count;
        name_[namePos_] = kGlyphs[index];
        drawn_ = false;
        return;
    }
    if (encoder && event.index == 2 && mode_ == Mode::Machine) {
        const int count = static_cast<int>(sizeof kMachineChoices);
        machineIndex_ = (machineIndex_ + (event.delta > 0 ? 1 : count - 1)) % count;
        drawn_ = false;
        return;
    }
    if (key && event.index == 2) confirm(nowMs);
}

void Console::applyEncoderKits() {
    for (int ch = 0; ch < kChannels; ++ch) {
        const int delta = synth_.takeKitDelta(ch);
        if (delta == 0) continue;
        const uint8_t kind = synth_.machineOnChannel(ch);
        if (kind != kitKind_[ch]) {
            kitKind_[ch] = kind;
            kitSlot_[ch] = 0;
        }
        int slot = kitSlot_[ch];
        int steps = delta > 0 ? delta : -delta;
        if (steps > 8) steps = 8;
        const int direction = delta > 0 ? 1 : -1;
        for (int i = 0; i < steps; ++i) {
            const int next = nextKit(ch, slot, direction);
            if (next <= 0) break;
            slot = next;
        }
        if (slot <= 0) continue;
        KitData kit;
        if (!recallKit(slot, kit)) continue;
        applyKit(ch, kit);
        send({SynthOp::FocusChannel, static_cast<uint8_t>(ch), 0, 0});
        synth_.setKitLabel(ch, kit.name);
        kitSlot_[ch] = slot;
        kitKind_[ch] = machineKind(kit.machine);
    }
}

void Console::tick(uint32_t nowMs) {
    applyEncoderKits();
    if (mode_ == Mode::Notice && static_cast<int32_t>(noticeUntilMs_ - nowMs) <= 0) {
        mode_ = Mode::Menu;
        drawn_ = false;
    }
    const uint32_t serial = synth_.editSerial();
    if (serial != seenSerial_) {
        seenSerial_ = serial;
        lastChangeMs_ = nowMs;
        pending_ = true;
    }
    if (!pending_ || !storage_ || !storage_->ready()) return;
    const bool quiet = nowMs - lastChangeMs_ >= kQuietMs;
    const bool due = nowMs - lastWriteMs_ >= kForceMs;
    if (!quiet && !due) return;
    saveState();
    lastWriteMs_ = nowMs;
    if (quiet) pending_ = false;
}

bool Console::update(Canvas& canvas, uint32_t nowMs) {
    if (mode_ == Mode::Play) {
        drawn_ = false;
        return false;
    }
    if (mode_ == Mode::Notice && static_cast<int32_t>(noticeUntilMs_ - nowMs) <= 0) {
        mode_ = Mode::Menu;
        drawn_ = false;
    }
    if (drawn_) return false;
    draw(canvas);
    drawn_ = true;
    return true;
}

void Console::draw(Canvas& canvas) {
    canvas.clear();
    if (mode_ == Mode::Notice) {
        canvas.text(0, 28, notice_, 15);
        return;
    }
    if (mode_ == Mode::Menu) {
        canvas.text(0, 0, "MENU", 15);
        canvas.fillRect(0, 9, Canvas::kWidth, 1, 3);
        for (int i = 0; i < kMenuCount; ++i) {
            const int y = 11 + i * 8;
            if (i == menuIndex_) canvas.fillRect(0, y - 1, Canvas::kWidth, 8, 15);
            canvas.text(2, y, kMenu[i], i == menuIndex_ ? 0 : 12);
        }
        return;
    }

    const char* title = "MACHINE";
    if (mode_ == Mode::SetSave) title = "SET SAVE";
    else if (mode_ == Mode::SetLoad) title = "SET LOAD";
    else if (mode_ == Mode::KitSave) title = "KIT SAVE";
    else if (mode_ == Mode::KitLoad) title = "KIT LOAD";
    canvas.text(0, 0, title, 15);
    canvas.fillRect(0, 9, Canvas::kWidth, 1, 3);

    if (mode_ == Mode::Machine) {
        char line[16];
        const int ch = synth_.focusedChannel() + 1;
        line[0] = 'C';
        line[1] = 'H';
        if (ch >= 10) {
            line[2] = static_cast<char>('0' + ch / 10);
            line[3] = static_cast<char>('0' + ch % 10);
            line[4] = '\0';
        } else {
            line[2] = static_cast<char>('0' + ch);
            line[3] = '\0';
        }
        canvas.text(0, 16, line, 15);
        canvas.text(0, 32, machineName(kMachineChoices[machineIndex_]), 15);
        drawKeys(canvas);
        return;
    }

    char slotText[12];
    char digits[3];
    twoDigits(slot_, digits);
    memcpy(slotText, "SLOT ", 5);
    slotText[5] = digits[0];
    slotText[6] = digits[1];
    slotText[7] = '\0';
    canvas.text(0, 16, slotText, 15);
    const bool naming = mode_ == Mode::SetSave || mode_ == Mode::KitSave;
    if (naming) {
        for (int i = 0; i < kNameLen; ++i) {
            const int x = i * Canvas::kCharAdvance;
            char glyph[2] = {name_[i] == ' ' ? '_' : name_[i], '\0'};
            if (i == namePos_) canvas.fillRect(x, 31, Canvas::kCharAdvance, 9, 15);
            canvas.text(x, 32, glyph, i == namePos_ ? 0 : 15);
        }
    } else if (preview_[0]) {
        canvas.text(0, 32, preview_, 10);
    }
    drawKeys(canvas);
}

void Console::drawKeys(Canvas& canvas) {
    canvas.text(0, 52, "K2 BACK", 8);
    const char* ok = "K3 OK";
    canvas.text(Canvas::kWidth - Canvas::textWidth(ok), 52, ok, 8);
}
