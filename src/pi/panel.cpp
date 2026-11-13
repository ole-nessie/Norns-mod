#include "panel.h"

namespace {

constexpr unsigned kA[3] = {4, 24, 12};
constexpr unsigned kB[3] = {27, 23, 25};
constexpr unsigned kKey[3] = {22, 26, 13};
constexpr unsigned kDebounceMs = 5;

// One detent is four quadrature transitions. Swap the A/B pins if a turn
// goes the wrong way.
const int8_t kQuad[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

}  // namespace

void Panel::begin() {
    for (int i = 0; i < 3; ++i) {
        a_[i].AssignPin(kA[i]);
        b_[i].AssignPin(kB[i]);
        key_[i].AssignPin(kKey[i]);
        a_[i].SetMode(GPIOModeInputPullUp, TRUE);
        b_[i].SetMode(GPIOModeInputPullUp, TRUE);
        key_[i].SetMode(GPIOModeInputPullUp, TRUE);
        enc_[i] = static_cast<u8>((a_[i].Read() << 1) | b_[i].Read());
        down_[i] = key_[i].Read() == LOW;
    }
    begun_ = true;
}

int Panel::poll(PanelEvent* out, int max, unsigned nowMs) {
    if (!begun_ || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < 3 && n < max; ++i) {
        const u8 now = static_cast<u8>((a_[i].Read() << 1) | b_[i].Read());
        accum_[i] += kQuad[(enc_[i] << 2) | now];
        enc_[i] = now;
        const int steps = accum_[i] / 4;
        if (steps != 0) {
            accum_[i] -= steps * 4;
            int8_t delta = static_cast<int8_t>(steps);
            if (steps > 127) delta = 127;
            if (steps < -128) delta = -128;
            out[n++] = {PanelControl::Encoder, static_cast<uint8_t>(i), delta};
        }
    }
    for (int i = 0; i < 3 && n < max; ++i) {
        const bool down = key_[i].Read() == LOW;
        if (down == down_[i]) continue;
        if (nowMs - changedMs_[i] < kDebounceMs) continue;
        changedMs_[i] = nowMs;
        down_[i] = down;
        if (down) out[n++] = {PanelControl::Key, static_cast<uint8_t>(i), 1};
    }
    return n;
}
