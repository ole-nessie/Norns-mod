#include <cstdio>
#include <string>

#include "display.h"
#include "ui/canvas.h"

namespace {

// xterm-256 colour for a 4-bit grey level.
int ansiGrey(uint8_t level) {
    return level == 0 ? 16 : 232 + (level * 23 + 7) / 15;
}

}  // namespace

TerminalDisplay::~TerminalDisplay() {
    if (opened_) std::fputs("\x1b[0m\x1b[?25h\n", stdout);
    std::fflush(stdout);
}

void TerminalDisplay::open() {
    std::fputs("\x1b[2J\x1b[?25l", stdout);
    opened_ = true;
}

void TerminalDisplay::show(const Canvas& c) {
    // Each character cell shows two pixel rows: upper half-block in the
    // foreground colour, lower pixel as the background colour.
    std::string out = "\x1b[H";
    out.reserve(Canvas::kWidth * Canvas::kHeight * 8);
    char code[32];
    for (int y = 0; y < Canvas::kHeight; y += 2) {
        int fg = -1, bg = -1;
        for (int x = 0; x < Canvas::kWidth; ++x) {
            const int top = ansiGrey(c.at(x, y));
            const int bottom = ansiGrey(c.at(x, y + 1));
            if (top != fg || bottom != bg) {
                std::snprintf(code, sizeof(code), "\x1b[38;5;%d;48;5;%dm", top, bottom);
                out += code;
                fg = top;
                bg = bottom;
            }
            out += "\u2580";
        }
        out += "\x1b[0m\n";
    }
    out += "m menu  [ ] ch  ; ' page  , . value  9/0 slot   a-k notes  q quit\n";
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
}
