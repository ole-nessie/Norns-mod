#pragma once

#include <stdint.h>

// 128x64 frame with 16 grey levels (0 = off, 15 = full), the norns OLED format.
class Canvas {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 64;
    static constexpr int kCharAdvance = 6;  // 5x7 glyphs plus one column of spacing
    static constexpr int kCharHeight = 8;

    void clear(uint8_t level = 0);
    void pixel(int x, int y, uint8_t level);
    void fillRect(int x, int y, int w, int h, uint8_t level);
    void text(int x, int y, const char* s, uint8_t level);
    static int textWidth(const char* s);

    uint8_t at(int x, int y) const { return pixels_[y * kWidth + x]; }

private:
    uint8_t pixels_[kWidth * kHeight] = {};
};
