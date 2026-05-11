#include "power.h"

#include "pins.h"

namespace power {

void begin() {
    pinMode(PIN_TP4056_STAT, INPUT_PULLUP);  // pullup prevents float when TP4056 not wired
    // PIN_BAT_ADC: analogRead works without pinMode on ESP32, divider TBD
}

uint8_t getBatteryPercent() {
    // TODO: replace with real divider math once VBAT divider ratio is confirmed.
    // Placeholder so the status bar renders something sane during HAL bring-up.
    return 75;
}

bool isCharging() {
    // STAT pin is active-LOW: LOW = charging, HIGH (floating-but-pulled-up) = full/idle.
    return digitalRead(PIN_TP4056_STAT) == LOW;
}

}  // namespace power
