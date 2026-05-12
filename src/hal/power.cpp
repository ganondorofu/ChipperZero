#include "power.h"

#include <Arduino.h>
#include "pins.h"

namespace power {

void begin() {
    pinMode(PIN_TP4056_STAT, INPUT_PULLUP);  // pullup prevents float when TP4056 not wired
    // PIN_BAT_ADC: analogRead works without pinMode on ESP32, divider TBD
}

uint8_t getBatteryPercent() {
    // 100kΩ/100kΩ divider: ADC sees VBAT/2.
    // VBAT range: 3.0V–4.2V → ADC range: 1.5V–2.1V
    // ESP32 ADC: 12-bit, 3.3V ref → ADC_in = V * 4095 / 3.3
    // 1.5V → 1861,  2.1V → 2606
    // Oversample 8× to reduce noise.
    constexpr int SAMPLES   = 8;
    constexpr int ADC_MIN   = 1861;  // 3.0V battery
    constexpr int ADC_MAX   = 2606;  // 4.2V battery

    long sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        sum += analogRead(PIN_BAT_ADC);
    }
    int adc = (int)(sum / SAMPLES);

    if (adc <= ADC_MIN) return 0;
    if (adc >= ADC_MAX) return 100;
    return (uint8_t)(((long)(adc - ADC_MIN) * 100) / (ADC_MAX - ADC_MIN));
}

bool isCharging() {
    // STAT pin is active-LOW: LOW = charging, HIGH (floating-but-pulled-up) = full/idle.
    return digitalRead(PIN_TP4056_STAT) == LOW;
}

}  // namespace power
