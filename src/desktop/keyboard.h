#pragma once

#include <termios.h>

// Non-blocking single-key reads from the controlling terminal, for testing
// without an Octatrack. Restores the terminal on destruction.
class TerminalKeyboard {
public:
    TerminalKeyboard();
    ~TerminalKeyboard();

    // Returns the next pressed key, or -1 if none is waiting.
    int poll();

private:
    bool active_ = false;
    termios saved_{};
};
