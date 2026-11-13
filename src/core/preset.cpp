#include "preset.h"

#include <string.h>

namespace {

constexpr uint16_t kVersion = 1;
constexpr uint16_t kKindSet = 1;
constexpr uint16_t kKindKit = 2;
constexpr int kKitVoiceBytes = 8 + 12 + 1 + kMaxParams;
constexpr int kMixBytes = kMixSlots;
constexpr int kKitBytes = kKitVoiceBytes + kMixBytes;
constexpr int kKitBytesV4 = kKitVoiceBytes + 4;

uint32_t crc32(const uint8_t* data, int len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void write16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void write32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t read32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

int writeKit(uint8_t* p, const KitData& kit) {
    memcpy(p, kit.machine, 8);
    memcpy(p + 8, kit.name, 12);
    p[20] = kit.count;
    memcpy(p + 21, kit.values, kMaxParams);
    memcpy(p + kKitVoiceBytes, kit.mix, kMixBytes);
    return kKitBytes;
}

void readKit(const uint8_t* p, KitData& kit, int mixBytes) {
    memset(&kit, 0, sizeof kit);
    memcpy(kit.machine, p, 8);
    memcpy(kit.name, p + 8, 12);
    kit.count = p[20];
    if (kit.count > kMaxParams) kit.count = kMaxParams;
    memcpy(kit.values, p + 21, kMaxParams);
    kit.mix[0] = 127;
    if (mixBytes > kMixSlots) mixBytes = kMixSlots;
    if (mixBytes > 0) memcpy(kit.mix, p + kKitVoiceBytes, mixBytes);
}

int encode(uint16_t kind, const uint8_t* payload, int payloadLen, uint32_t sequence, uint8_t* dst,
           int cap) {
    const int total = 16 + payloadLen + 4;
    if (total > cap) return 0;
    memcpy(dst, "DRBX", 4);
    write16(dst + 4, kVersion);
    write16(dst + 6, kind);
    write32(dst + 8, sequence);
    write16(dst + 12, static_cast<uint16_t>(payloadLen));
    write16(dst + 14, 0);
    memcpy(dst + 16, payload, payloadLen);
    write32(dst + 16 + payloadLen, crc32(dst, 16 + payloadLen));
    return total;
}

bool decode(const uint8_t* src, int len, uint16_t expectKind, const uint8_t** payload, int* payloadLen,
            uint32_t* sequence) {
    if (len < 20 || memcmp(src, "DRBX", 4) != 0) return false;
    if (read16(src + 4) != kVersion || read16(src + 6) != expectKind) return false;
    const int n = read16(src + 12);
    if (n < 0 || 16 + n + 4 != len) return false;
    if (read32(src + 16 + n) != crc32(src, 16 + n)) return false;
    *payload = src + 16;
    *payloadLen = n;
    *sequence = read32(src + 8);
    return true;
}

}  // namespace

int encodeSet(const SetData& set, uint32_t sequence, uint8_t* dst, int cap) {
    uint8_t payload[12 + kChannels * kKitBytes + kFxParams];
    memcpy(payload, set.name, 12);
    for (int i = 0; i < kChannels; ++i) writeKit(payload + 12 + i * kKitBytes, set.channels[i]);
    memcpy(payload + 12 + kChannels * kKitBytes, set.fx, kFxParams);
    return encode(kKindSet, payload, static_cast<int>(sizeof payload), sequence, dst, cap);
}

int encodeKit(const KitData& kit, uint32_t sequence, uint8_t* dst, int cap) {
    uint8_t payload[kKitBytes];
    writeKit(payload, kit);
    return encode(kKindKit, payload, kKitBytes, sequence, dst, cap);
}

bool decodeSet(const uint8_t* src, int len, SetData& set, uint32_t& sequence) {
    const uint8_t* payload = nullptr;
    int n = 0;
    if (!decode(src, len, kKindSet, &payload, &n, &sequence)) return false;
    // 8- and 15-channel saves from before the channel strip still load.
    int channels = 0;
    int kitBytes = 0;
    int mixBytes = 0;
    bool withFx = false;
    if (n == 12 + 8 * kKitVoiceBytes) {
        channels = 8;
        kitBytes = kKitVoiceBytes;
    } else if (n == 12 + kChannels * kKitVoiceBytes) {
        channels = kChannels;
        kitBytes = kKitVoiceBytes;
    } else if (n == 12 + kChannels * kKitBytesV4 + kFxParams) {
        channels = kChannels;
        kitBytes = kKitBytesV4;
        mixBytes = 4;
        withFx = true;
    } else if (n == 12 + kChannels * kKitBytes + kFxParams) {
        channels = kChannels;
        kitBytes = kKitBytes;
        mixBytes = kMixBytes;
        withFx = true;
    }
    if (channels == 0) return false;
    memset(&set, 0, sizeof set);
    for (int i = 0; i < kChannels; ++i) set.channels[i].mix[0] = 127;
    for (int i = 0; i < kFxParams; ++i) set.fx[i] = fxDefault(i);
    memcpy(set.name, payload, 12);
    for (int i = 0; i < channels; ++i) readKit(payload + 12 + i * kitBytes, set.channels[i], mixBytes);
    if (withFx) memcpy(set.fx, payload + 12 + channels * kitBytes, kFxParams);
    return true;
}

bool decodeKit(const uint8_t* src, int len, KitData& kit, uint32_t& sequence) {
    const uint8_t* payload = nullptr;
    int n = 0;
    if (!decode(src, len, kKindKit, &payload, &n, &sequence)) return false;
    int mixBytes = -1;
    if (n == kKitVoiceBytes) mixBytes = 0;
    else if (n == kKitBytesV4) mixBytes = 4;
    else if (n == kKitBytes) mixBytes = kMixBytes;
    if (mixBytes < 0) return false;
    readKit(payload, kit, mixBytes);
    return true;
}

namespace {

struct FactoryDrum {
    const char* name;
    uint8_t values[8];
};

// Sophie factory pads, mapped onto METAL's eight parameters:
// MODL TUNE COLR METL FDBK SWEP DEC CRSH.
// Drive stays at the machine default; these knobs are the whole kit.
const FactoryDrum kFactory[16] = {
    {"KICK", {11, 51, 30, 10, 8, 99, 58, 0}},
    {"RIM", {75, 64, 97, 91, 43, 74, 29, 61}},
    {"SNARE", {75, 64, 66, 97, 23, 82, 54, 38}},
    {"CLAP", {53, 64, 107, 112, 53, 59, 48, 71}},
    {"SNARE 2", {32, 64, 80, 104, 36, 79, 47, 48}},
    {"LOW TOM", {11, 64, 30, 56, 20, 101, 68, 10}},
    {"CL HAT", {75, 64, 119, 124, 74, 64, 16, 66}},
    {"FL TOM", {11, 64, 39, 61, 25, 99, 71, 13}},
    {"PD HAT", {75, 64, 112, 119, 63, 65, 31, 58}},
    {"MID TOM", {11, 64, 48, 66, 28, 95, 64, 15}},
    {"OP HAT", {75, 64, 116, 122, 79, 61, 72, 53}},
    {"LM TOM", {11, 64, 57, 71, 32, 92, 61, 18}},
    {"HM TOM", {11, 64, 65, 76, 36, 90, 57, 20}},
    {"CRASH", {32, 64, 99, 117, 91, 56, 104, 36}},
    {"HI TOM", {11, 64, 74, 81, 38, 87, 53, 23}},
    {"RIDE", {53, 64, 88, 109, 84, 60, 95, 28}},
};

// 808 kits. MODL TUNE DEC TONE SNAP ATTK ACC VOL.
const FactoryDrum kAnalog[12] = {
    {"808 BD", {8, 64, 80, 48, 100, 80, 110, 108}},
    {"808 SD", {27, 64, 74, 78, 92, 70, 105, 100}},
    {"808 CH", {45, 68, 16, 88, 22, 55, 110, 110}},
    {"808 OH", {45, 62, 88, 80, 30, 45, 105, 100}},
    {"808 CP", {82, 64, 76, 80, 85, 95, 105, 100}},
    {"808 RS", {100, 70, 24, 72, 70, 110, 100, 96}},
    {"808 CB", {118, 64, 42, 90, 18, 55, 100, 100}},
    {"808 LT", {64, 42, 82, 52, 48, 55, 100, 104}},
    {"808 MT", {64, 64, 74, 60, 40, 48, 100, 104}},
    {"808 HT", {64, 92, 64, 70, 34, 42, 100, 104}},
    {"808 CY", {45, 48, 112, 64, 40, 35, 95, 96}},
    {"808 PH", {45, 66, 46, 84, 26, 50, 105, 104}},
};

// EFM drums. MODL TUNE DEC RAMP TONE NOIS MOD VOL.
const FactoryDrum kEfm[12] = {
    {"EFM BD", {0, 64, 84, 108, 28, 14, 42, 112}},
    {"EFM SD", {20, 68, 62, 70, 88, 110, 58, 108}},
    {"EFM LT", {40, 46, 80, 78, 36, 8, 28, 108}},
    {"EFM HT", {56, 84, 64, 60, 48, 8, 34, 108}},
    {"EFM CH", {72, 74, 22, 18, 108, 118, 96, 102}},
    {"EFM OH", {88, 66, 74, 16, 96, 116, 84, 100}},
    {"EFM CP", {104, 64, 58, 8, 72, 124, 18, 104}},
    {"EFM RS", {120, 76, 20, 24, 84, 36, 78, 104}},
    {"EFM BD2", {0, 36, 104, 118, 22, 8, 30, 114}},
    {"EFM SD2", {20, 88, 42, 90, 100, 96, 70, 106}},
    {"EFM MT", {40, 72, 70, 66, 44, 10, 32, 108}},
    {"EFM CY", {88, 58, 110, 12, 110, 120, 70, 98}},
};

}  // namespace

bool factoryKit(int slot, KitData& kit) {
    const FactoryDrum* drum = nullptr;
    const char* machine = nullptr;
    if (slot >= 1 && slot <= 16) {
        drum = &kFactory[slot - 1];
        machine = "METL";
    } else if (slot >= 17 && slot <= 28) {
        drum = &kAnalog[slot - 17];
        machine = "A808";
    } else if (slot >= 29 && slot <= 40) {
        drum = &kEfm[slot - 29];
        machine = "EFMD";
    } else {
        return false;
    }
    memset(&kit, 0, sizeof kit);
    kit.mix[0] = 127;
    for (int i = 0; machine[i] && i < 7; ++i) kit.machine[i] = machine[i];
    for (int i = 0; drum->name[i] && i < 11; ++i) kit.name[i] = drum->name[i];
    kit.count = 8;
    memcpy(kit.values, drum->values, 8);
    return true;
}
