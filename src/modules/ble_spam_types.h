#pragma once
#include <stdint.h>

// Spam target type shared by BleSpamModule (ESP32) and NrfBleSpamModule (NRF24).
enum class BleSpamType : uint8_t { ALL = 0, APPLE, GOOGLE, SAMSUNG, MICROSOFT };
