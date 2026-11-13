#pragma once

#include <stdint.h>

#include "fx.h"
#include "machines.h"
#include "voice.h"

constexpr int kSetSlots = 16;
constexpr int kKitSlots = 64;

// Packed by preset.cpp. Do not fwrite the struct itself.
struct KitData {
    char machine[8] = {};
    char name[12] = {};
    uint8_t count = 0;
    uint8_t values[kMaxParams] = {};
    // Stored order: mix volume, delay, reverb, chorus, drive. The screen shows the sends first.
    uint8_t mix[kMixSlots] = {127, 0, 0, 0, 0};
};

struct SetData {
    char name[12] = {};
    KitData channels[kChannels] = {};
    uint8_t fx[kFxParams] = {};
};

// File image is at most this many bytes. A set is the name, one kit per channel,
// and the master effect settings.
constexpr int kPresetBytes = 1024;

// Returns the number of bytes written, or 0 if `cap` is too small.
int encodeSet(const SetData& set, uint32_t sequence, uint8_t* dst, int cap);
int encodeKit(const KitData& kit, uint32_t sequence, uint8_t* dst, int cap);

// False when the magic, version, kind, length or CRC does not match.
bool decodeSet(const uint8_t* src, int len, SetData& set, uint32_t& sequence);
bool decodeKit(const uint8_t* src, int len, KitData& kit, uint32_t& sequence);

// Built-in METAL drums for kit slots 1-16, 808 drums for 17-28, and EFM drums
// for 29-40. A saved kit in that slot wins.
bool factoryKit(int slot, KitData& kit);
