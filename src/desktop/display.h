#pragma once

class Canvas;

// Draws the 128x64 frame in the terminal with ANSI greys (needs >= 128 columns).
class TerminalDisplay {
public:
    ~TerminalDisplay();
    void open();
    void show(const Canvas& canvas);

private:
    bool opened_ = false;
};
