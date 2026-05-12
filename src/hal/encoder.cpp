#include "encoder.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "pins.h"

namespace encoder {

namespace {

constexpr uint32_t kDebounceMs = 5;
constexpr uint32_t kLongPressMs = 600;
constexpr UBaseType_t kQueueLen = 8;

QueueHandle_t g_injectQueue = nullptr;

uint8_t g_lastEncState = 0;

struct Button {
    bool     stableLevel;
    bool     lastReadLevel;
    uint32_t lastChangeMs;
    uint32_t pressStartMs;
    bool     longSent;
};

Button g_ok   = {HIGH, HIGH, 0, 0, false};
Button g_back = {HIGH, HIGH, 0, 0, false};

uint8_t readEncState() {
    return (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
}

InputEvent pollEncoder() {
    uint8_t cur = readEncState();
    if (cur == g_lastEncState) return EVENT_NONE;

    static const int8_t kDelta[16] = {
         0, -1, +1,  0,
        +1,  0,  0, -1,
        -1,  0,  0, +1,
         0, +1, -1,  0,
    };
    static int8_t accum = 0;
    accum += kDelta[(g_lastEncState << 2) | cur];
    g_lastEncState = cur;

    if (accum >= 4)  { accum = 0; return EVENT_RIGHT; }
    if (accum <= -4) { accum = 0; return EVENT_LEFT; }
    if (accum > 8 || accum < -8) accum = 0;
    return EVENT_NONE;
}

static InputEvent pollOneButton(Button& btn, int pin,
                                InputEvent shortEv, InputEvent longEv) {
    uint32_t now = millis();
    bool level = digitalRead(pin);
    if (level != btn.lastReadLevel) {
        btn.lastReadLevel = level;
        btn.lastChangeMs = now;
    }
    if ((now - btn.lastChangeMs) >= kDebounceMs && level != btn.stableLevel) {
        btn.stableLevel = level;
        if (level == LOW) {
            btn.pressStartMs = now;
            btn.longSent = false;
        } else {
            if (!btn.longSent) return shortEv;
        }
    }
    if (longEv != EVENT_NONE &&
        btn.stableLevel == LOW && !btn.longSent &&
        (now - btn.pressStartMs) >= kLongPressMs) {
        btn.longSent = true;
        return longEv;
    }
    return EVENT_NONE;
}

InputEvent pollButtons() {
    InputEvent ev = pollOneButton(g_ok,   PIN_BTN_OK,   EVENT_OK,   EVENT_BACK);
    if (ev != EVENT_NONE) return ev;
    ev = pollOneButton(g_back, PIN_BTN_BACK, EVENT_BACK, EVENT_NONE);
    return ev;
}

}  // namespace

void begin() {
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    pinMode(PIN_BTN_OK,   INPUT_PULLUP);
    pinMode(PIN_BTN_BACK, INPUT_PULLUP);
    g_lastEncState = readEncState();
    if (!g_injectQueue) {
        g_injectQueue = xQueueCreate(kQueueLen, sizeof(InputEvent));
    }
}

InputEvent tick() {
    InputEvent ev = pollEncoder();
    if (ev != EVENT_NONE) return ev;
    ev = pollButtons();
    if (ev != EVENT_NONE) return ev;
    if (g_injectQueue) {
        InputEvent queued;
        if (xQueueReceive(g_injectQueue, &queued, 0) == pdTRUE) {
            return queued;
        }
    }
    return EVENT_NONE;
}

bool injectEvent(InputEvent ev) {
    if (!g_injectQueue) return false;
    if (ev == EVENT_NONE) return true;
    return xQueueSend(g_injectQueue, &ev, 0) == pdTRUE;
}

}  // namespace encoder
