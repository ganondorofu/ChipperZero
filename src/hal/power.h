#pragma once

#include <Arduino.h>

namespace power {

void begin();

uint8_t getBatteryPercent();

bool isCharging();

}  // namespace power
