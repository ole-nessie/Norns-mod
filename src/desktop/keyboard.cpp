#include "keyboard.h"

#include <unistd.h>

TerminalKeyboard::TerminalKeyboard() {
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_) != 0) return;
    termios raw = saved_;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    active_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
}

TerminalKeyboard::~TerminalKeyboard() {
    if (active_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
}

int TerminalKeyboard::poll() {
    if (!active_) return -1;
    unsigned char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}
