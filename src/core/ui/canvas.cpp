#include "ui/canvas.h"

#include <string.h>

#include "glcdfont.h"

void Canvas::clear(uint8_t level) {
    memset(pixels_, level & 0x0F, sizeof(pixels_));
}

void Canvas::pixel(int x, int y, uint8_t level) {
    if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
    pixels_[y * kWidth + x] = level & 0x0F;
}

void Canvas::fillRect(int x, int y, int w, int h, uint8_t level) {
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) pixel(xx, yy, level);
    }
}

void Canvas::text(int x, int y, const char* s, uint8_t level) {
    for (; *s; ++s, x += kCharAdvance) {
        // 5 column bytes per character, least significant bit at the top.
        const unsigned char* columns = font + static_cast<unsigned char>(*s) * 5;
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < kCharHeight; ++row) {
                if (columns[col] & (1u << row)) pixel(x + col, y + row, level);
            }
        }
    }
}

int Canvas::textWidth(const char* s) {
    const int n = static_cast<int>(strlen(s));
    return n > 0 ? n * kCharAdvance - 1 : 0;
}
