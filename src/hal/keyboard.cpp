#include "keyboard.h"

#include <string.h>
#include <Arduino.h>
#include "../hal/display.h"
#include "../hal/encoder.h"

namespace keyboard {

// " " + a-z + 0-9 + symbols + A-Z
static const char kChars[] =
    " abcdefghijklmnopqrstuvwxyz"
    "0123456789"
    ".-_@#!?/:"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static const uint8_t kNormalLen = sizeof(kChars) - 1;
static const uint8_t kTotal     = kNormalLen + 2;  // +BS +OK

static inline bool isBS(uint8_t i) { return i == kNormalLen; }
static inline bool isOK(uint8_t i) { return i == kNormalLen + 1; }

void init(State& s) {
    memset(s.buf, 0, sizeof(s.buf));
    s.len     = 0;
    s.charIdx = 0;
    s.done    = false;
    s.ok      = false;
}

void handleEvent(State& s, uint8_t ev) {
    switch (static_cast<encoder::InputEvent>(ev)) {
        case encoder::EVENT_LEFT:
            s.charIdx = (s.charIdx > 0) ? s.charIdx - 1 : kTotal - 1;
            break;
        case encoder::EVENT_RIGHT:
            s.charIdx = (s.charIdx + 1 < kTotal) ? s.charIdx + 1 : 0;
            break;
        case encoder::EVENT_OK:
            if (isBS(s.charIdx)) {
                if (s.len > 0) s.buf[--s.len] = '\0';
            } else if (isOK(s.charIdx)) {
                s.done = true;
                s.ok   = true;
            } else {
                if (s.len < kMaxLen) {
                    s.buf[s.len++] = kChars[s.charIdx];
                    s.buf[s.len]   = '\0';
                }
            }
            break;
        case encoder::EVENT_BACK:
            if (s.len > 0) {
                s.buf[--s.len] = '\0';
            } else {
                s.done = true;
                s.ok   = false;
            }
            break;
        default:
            break;
    }
}

// ---- Render -----------------------------------------------------------------
// Layout on 128×64 SH1106:
//   y= 0-11  header bar ("Keyboard" + divider)
//   y=12-20  prompt   (5x7 font, baseline 20)
//   y=21-33  input    (6x10 font, baseline 33)
//   y=34     divider
//   y=36-51  char picker (9 cells × 14px wide, 16px tall)
//   y=56-63  hint (5x7 font, baseline 63)

constexpr uint8_t kCellW   = 14;
constexpr uint8_t kCellH   = 16;
constexpr uint8_t kVisible = 9;
constexpr uint8_t kHalf    = kVisible / 2;  // = 4
constexpr uint8_t kStartX  = (128 - kVisible * kCellW) / 2;  // = 1
constexpr uint8_t kPickerY = 36;

static const char* cellLabel(uint8_t idx, char* tmp) {
    if (isBS(idx)) return "<-";
    if (isOK(idx)) return "OK";
    tmp[0] = kChars[idx];
    tmp[1] = '\0';
    return tmp;
}

void render(const State& s, const char* prompt) {
    auto& g = display::u8g2();
    g.clearBuffer();

    // Header bar
    g.setFont(u8g2_font_6x10_tf);
    g.drawStr(0, 9, "Keyboard");
    g.drawHLine(0, 11, 128);

    // Prompt
    g.setFont(u8g2_font_5x7_tf);
    if (prompt && prompt[0]) {
        g.drawStr(0, 20, prompt);
    }

    // Input buffer with cursor (scroll to show last 20 chars)
    g.setFont(u8g2_font_6x10_tf);
    {
        char disp[kMaxLen + 4];
        snprintf(disp, sizeof(disp), "%s|", s.buf);
        const char* p = disp;
        // Each char ~6px wide, 21 chars fit in 126px
        if (s.len >= 21) p = disp + (s.len - 20);
        g.drawStr(0, 33, p);
    }

    // Divider below input
    g.drawHLine(0, 34, 128);

    // Char picker
    for (uint8_t slot = 0; slot < kVisible; slot++) {
        int32_t off = (int32_t)slot - kHalf;
        uint8_t idx = (uint8_t)(((int32_t)s.charIdx + off + kTotal * 4) % kTotal);
        uint8_t cx  = kStartX + slot * kCellW;

        char tmp[3];
        const char* lbl = cellLabel(idx, tmp);
        uint8_t lw = g.getStrWidth(lbl);
        uint8_t tx = cx + (kCellW - lw) / 2;

        if (slot == kHalf) {
            g.drawRBox(cx, kPickerY, kCellW, kCellH, 2);
            g.setDrawColor(0);
            g.drawStr(tx, kPickerY + 12, lbl);
            g.setDrawColor(1);
        } else {
            g.drawStr(tx, kPickerY + 12, lbl);
        }
    }

    // Hint
    g.setFont(u8g2_font_5x7_tf);
    g.drawStr(0, 63, "L/R:move OK:sel BCK:del");

    display::markDirty();
}

}  // namespace keyboard
