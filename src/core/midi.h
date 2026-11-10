#pragma once

// Portable core: C headers and compiler builtins only, so it also builds
// bare metal under Circle (no C++ standard library there).
#include <stddef.h>
#include <stdint.h>

struct MidiMessage {
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;

    uint8_t type() const { return status & 0xF0; }
    uint8_t channel() const { return status & 0x0F; }
};

// Lock-free single-producer / single-consumer ring buffer. The consumer is the
// audio callback, so pop() never blocks.
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push(const T& item) {
        const size_t head = __atomic_load_n(&head_, __ATOMIC_RELAXED);
        const size_t tail = __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
        if (head - tail == Capacity) return false;
        buf_[head & (Capacity - 1)] = item;
        __atomic_store_n(&head_, head + 1, __ATOMIC_RELEASE);
        return true;
    }

    bool pop(T& out) {
        const size_t tail = __atomic_load_n(&tail_, __ATOMIC_RELAXED);
        const size_t head = __atomic_load_n(&head_, __ATOMIC_ACQUIRE);
        if (head == tail) return false;
        out = buf_[tail & (Capacity - 1)];
        __atomic_store_n(&tail_, tail + 1, __ATOMIC_RELEASE);
        return true;
    }

private:
    T buf_[Capacity] = {};
    alignas(64) size_t head_ = 0;
    alignas(64) size_t tail_ = 0;
};

using MidiQueue = SpscQueue<MidiMessage, 1024>;

// Turns a raw MIDI byte stream (DIN / UART) into channel messages, including
// running status. System real-time and system common bytes are dropped.
class MidiParser {
public:
    // Returns true when `out` holds a complete message.
    bool feed(uint8_t byte, MidiMessage& out);

private:
    uint8_t status_ = 0;
    uint8_t data_[2] = {};
    int count_ = 0;
};
