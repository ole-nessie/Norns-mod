#include "ui/param_page.h"

#include <string.h>

#include "fx.h"
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
           triggerFlash == o.triggerFlash && master == o.master && strip == o.strip &&
           memcmp(kitName, o.kitName, sizeof(kitName)) == 0;
}

void drawCells(Canvas& c, const PageState& s, const ParamInfo* params, int count, bool fx) {
    for (int i = 0; i < count && i < kParamsPerPage; ++i) {
        if (!params[i].name) continue;
        char buf[6];
        char number[4];
        const char* text = nullptr;
        if (fx) text = formatFxValue(fxParamIndex(s.page, i), s.values[i], buf);
        if (!text) text = formatNumber(s.values[i], number);
        drawCell(c, i, params[i].name, text, s.values[i], i == s.lastTouched);
    }
}

void drawParamPage(Canvas& c, const PageState& s) {
    c.clear();
    c.fillRect(Canvas::kWidth - 7, 1, 6, 6, s.triggerFlash ? 15 : 2);
    c.fillRect(0, 9, Canvas::kWidth, 1, 3);

    char channel[4];
    if (s.master) {
        c.text(0, 0, "CH", 15);
        const char* ch = formatNumber(s.midiChannel + 1, channel);
        c.text(2 * Canvas::kCharAdvance, 0, ch, 15);
        const char* pageName = fxPageName(s.page);
        c.text(Canvas::kWidth - 10 - Canvas::textWidth(pageName), 0, pageName, 9);
        drawCells(c, s, fxParams(s.page), fxSlots(s.page), true);
        return;
    }

    c.text(0, 0, "CH", 15);
    const char* ch = formatNumber(s.midiChannel + 1, channel);
    c.text(2 * Canvas::kCharAdvance, 0, ch, 15);
    const int label = (3 + static_cast<int>(strlen(ch))) * Canvas::kCharAdvance;
    if (!s.voice) {
        c.text(label, 0, "EMPTY", 8);
        return;
    }

    c.text(label, 0, s.kitName[0] ? s.kitName : s.voice->name(), 15);
    const char* pageName = s.strip ? "MIX" : s.voice->pageName(s.page);
    c.text(Canvas::kWidth - 10 - Canvas::textWidth(pageName), 0, pageName, 9);
    if (s.strip) {
        drawCells(c, s, mixParams(), kMixSlots, false);
        return;
    }

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

    const uint32_t midi = synth_.midiCount();
    if (midi != seenTriggers_) {
        seenTriggers_ = midi;
        flashUntilMs_ = nowMs + kFlashMs;
    }

    PageState state;
    state.midiChannel = ch;
    const char* kit = synth_.kitLabel(ch);
    for (int i = 0; i < 8 && kit[i]; ++i) state.kitName[i] = kit[i];
    state.triggerFlash = static_cast<int32_t>(flashUntilMs_ - nowMs) > 0;
    if (synth_.editingFx()) {
        state.master = true;
        state.page = synth_.fxPage();
        const int first = fxParamIndex(state.page, 0);
        const int slots = fxSlots(state.page);
        for (int i = 0; i < slots; ++i) state.values[i] = synth_.fxValue(fxParamIndex(state.page, i));
        const int touched = synth_.fxTouched();
        state.lastTouched = touched < first || touched >= first + slots ? -1 : touched - first;
    } else if (const Voice* voice = synth_.voiceOnChannel(ch)) {
        const int touched = view.lastTouched();
        state.voice = voice;
        state.page = view.page();
        const int voicePages = synth_.voicePages(ch);
        if (state.page >= voicePages) {
            state.strip = true;
            state.page = voicePages;
            for (int i = 0; i < kMixSlots; ++i) state.values[i] = synth_.mixValue(ch, mixStoreIndex(i));
            const int first = voicePages * kParamsPerPage;
            state.lastTouched = touched < first || touched >= first + kMixSlots ? -1 : touched - first;
        } else {
            const int first = state.page * kParamsPerPage;
            for (int i = 0; i < kParamsPerPage && first + i < voice->paramCount(); ++i) {
                state.values[i] = view.value(first + i);
            }
            state.lastTouched = touched < 0 ? -1 : touched - first;
        }
    }

    if (drawn_ && state == shown_) return false;
    drawParamPage(canvas, state);
    shown_ = state;
    drawn_ = true;
    return true;
}
