#include "ui/param_page.h"

#include <string.h>

#include "synth.h"
#include "ui/canvas.h"

namespace {

constexpr int kColumns = 4;
constexpr int kCellWidth = Canvas::kWidth / kColumns;
constexpr int kRowTop[2] = {12, 37};
constexpr int kCellHeight = 22;
constexpr uint32_t kFlashMs = 80;

// Writes 0..999 as decimal; returns `buf`.
char* formatNumber(int value, char (&buf)[4]) {
    char* p = buf + 3;
    *p = '\0';
    do {
        *--p = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value > 0 && p > buf);
    return p;
}

void drawCentered(Canvas& c, int cellX, int y, const char* s, uint8_t level) {
    c.text(cellX + (kCellWidth - Canvas::textWidth(s)) / 2, y, s, level);
}

void drawCell(Canvas& c, int index, const char* name, const char* text, uint8_t value,
              bool touched) {
    const int x = (index % kColumns) * kCellWidth;
    const int y = kRowTop[index / kColumns];
    const uint8_t fg = touched ? 0 : 15;
    if (touched) c.fillRect(x, y - 1, kCellWidth, kCellHeight - 3, 15);

    drawCentered(c, x, y, name, touched ? 0 : 6);
    drawCentered(c, x, y + 9, text, fg);

    const int barY = y + kCellHeight - 2;
    const int barWidth = kCellWidth - 4;
    c.fillRect(x + 2, barY, barWidth, 2, 2);
    c.fillRect(x + 2, barY, value * barWidth / 127, 2, 12);
}

}  // namespace

bool PageState::operator==(const PageState& o) const {
    return voice == o.voice && midiChannel == o.midiChannel && page == o.page &&
           memcmp(values, o.values, sizeof(values)) == 0 && lastTouched == o.lastTouched &&
           triggerFlash == o.triggerFlash;
}

void drawParamPage(Canvas& c, const PageState& s) {
    c.clear();

    char channel[4];
    c.text(0, 0, "CH", 15);
    const char* ch = formatNumber(s.midiChannel + 1, channel);
    c.text(2 * Canvas::kCharAdvance, 0, ch, 15);
    c.fillRect(Canvas::kWidth - 7, 1, 6, 6, s.triggerFlash ? 15 : 2);
    c.fillRect(0, 9, Canvas::kWidth, 1, 3);
    if (!s.voice) return;

    c.text((3 + static_cast<int>(strlen(ch))) * Canvas::kCharAdvance, 0, s.voice->name(), 15);
    const char* pageName = s.voice->pageName(s.page);
    c.text(Canvas::kWidth - 10 - Canvas::textWidth(pageName), 0, pageName, 9);

    const int first = s.page * kParamsPerPage;
    const ParamInfo* params = s.voice->params();
    for (int i = 0; i < kParamsPerPage && first + i < s.voice->paramCount(); ++i) {
        if (!params[first + i].name) continue;
        char buf[6];
        const char* text = s.voice->formatValue(first + i, s.values[i], buf);
        char number[4];
        if (!text) text = formatNumber(s.values[i], number);
        drawCell(c, i, params[first + i].name, text, s.values[i], i == s.lastTouched);
    }
}

bool PageView::update(Canvas& canvas, uint32_t nowMs) {
    const int ch = synth_.focusedChannel();
    const VoiceView& view = synth_.view(ch);

    const uint32_t triggers = view.triggerCount();
    if (triggers != seenTriggers_) {
        seenTriggers_ = triggers;
        flashUntilMs_ = nowMs + kFlashMs;
    }

    PageState state;
    state.midiChannel = ch;
    if (const Voice* voice = synth_.voiceOnChannel(ch)) {
        const int touched = view.lastTouched();
        state.voice = voice;
        state.page = touched < 0 ? 0 : touched / kParamsPerPage;
        const int first = state.page * kParamsPerPage;
        for (int i = 0; i < kParamsPerPage && first + i < voice->paramCount(); ++i) {
            state.values[i] = view.value(first + i);
        }
        state.lastTouched = touched < 0 ? -1 : touched - first;
    }
    state.triggerFlash = static_cast<int32_t>(flashUntilMs_ - nowMs) > 0;

    if (drawn_ && state == shown_) return false;
    drawParamPage(canvas, state);
    shown_ = state;
    drawn_ = true;
    return true;
}
