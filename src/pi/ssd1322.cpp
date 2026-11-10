#include "ssd1322.h"

#include <circle/timer.h>

#include "ui/canvas.h"

namespace {

constexpr unsigned kChipSelect = 0;
constexpr unsigned kDcPin = 5;
constexpr unsigned kResetPin = 6;

struct InitStep {
    u8 cmd;
    u8 len;
    u8 data[2];
};

// Same register values norns uses (matron/src/hardware/screen/ssd1322.cc),
// with the norns shield orientation.
constexpr InitStep kInit[] = {
    {0xAE, 0, {}},            // display off
    {0xB9, 0, {}},            // default linear grey scale
    {0xB3, 1, {0x91}},        // oscillator frequency
    {0xCA, 1, {0x3F}},        // multiplex ratio
    {0xA2, 1, {0x00}},        // display offset
    {0xA1, 1, {0x00}},        // start line
    {0xAB, 1, {0x01}},        // VDD regulator
    {0xB4, 2, {0xA0, 0xFD}},  // display enhancement A
    {0xC1, 1, {0x7F}},        // contrast
    {0xC7, 1, {0x0F}},        // master current
    {0xB1, 1, {0xF2}},        // phase length
    {0xBB, 1, {0x1F}},        // precharge voltage
    {0xBE, 1, {0x04}},        // VCOMH
    {0xA6, 0, {}},            // normal display mode
    {0xA0, 2, {0x04, 0x11}},  // remap: shield orientation, dual COM
};

}  // namespace

Ssd1322::Ssd1322(CSPIMaster* spi)
    : spi_(spi), dc_(kDcPin, GPIOModeOutput), reset_(kResetPin, GPIOModeOutput) {
    reset_.Write(LOW);
}

void Ssd1322::command(u8 cmd, const u8* data, unsigned len) {
    dc_.Write(LOW);
    spi_->Write(kChipSelect, &cmd, 1);
    if (len > 0) {
        dc_.Write(HIGH);
        spi_->Write(kChipSelect, data, len);
    }
}

boolean Ssd1322::Initialize() {
    CTimer::SimpleMsDelay(1);
    reset_.Write(HIGH);
    CTimer::SimpleMsDelay(1);
    for (const InitStep& step : kInit) command(step.cmd, step.data, step.len);
    return TRUE;
}

void Ssd1322::Show(const Canvas& c) {
    if (!on_) {
        command(0xAF);  // display on, after the first frame is ready
        on_ = TRUE;
    }
    static const u8 kColumns[] = {28, 91};
    static const u8 kRows[] = {0, 63};
    command(0x15, kColumns, 2);
    command(0x75, kRows, 2);
    command(0x5C);  // write RAM

    // One byte per pixel with the 4-bit level in both nibbles, as norns sends it.
    for (int y = 0; y < Canvas::kHeight; ++y) {
        for (int x = 0; x < Canvas::kWidth; ++x) {
            const u8 level = c.at(x, y);
            frame_[y * Canvas::kWidth + x] = static_cast<u8>((level << 4) | level);
        }
    }
    dc_.Write(HIGH);
    spi_->Write(kChipSelect, frame_, sizeof frame_);
}
