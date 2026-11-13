#pragma once

#include <stdint.h>

// Parameters come in pages of 8, matching the 8 encoders of an Elektron page.
constexpr int kParamsPerPage = 8;
constexpr int kMaxPages = 3;
constexpr int kMaxParams = kParamsPerPage * kMaxPages;

struct ParamInfo {
    const char* name;  // at most 4 characters; nullptr leaves the slot empty
    uint8_t initial;   // 0..127
};

// A sound engine ("machine"). Everything except the const display helpers is
// called from the audio side only, so it must not block or allocate.
class Voice {
public:
    virtual ~Voice() = default;

    virtual const char* name() const = 0;
    virtual int paramCount() const { return kParamsPerPage; }
    virtual const ParamInfo* params() const = 0;  // paramCount() entries
    virtual const char* pageName(int /*page*/) const { return ""; }

    // Text for the OLED instead of the plain 0..127 number, or nullptr.
    // At most 4 characters, written into `buf` or returned as a literal.
    virtual const char* formatValue(int /*index*/, uint8_t /*value*/, char* /*buf*/) const {
        return nullptr;
    }

    virtual void prepare(float sampleRate) = 0;
    virtual void setParam(int index, uint8_t value) = 0;
    virtual void noteOn(uint8_t note, float velocity) = 0;
    virtual void noteOff(uint8_t /*note*/) {}
    // Release every held note, the same as note-off. One-shot drums ignore this.
    virtual void releaseAll() {}
    // Adds mono output to `out`.
    virtual void render(float* out, int frames) = 0;
};
