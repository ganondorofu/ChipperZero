#pragma once

// BLOG(fmt, ...) -- log to both USB Serial and BLE log characteristic.
// Usage is identical to Serial.printf.  Safe to call before BLE connects
// (the BLE part is silently dropped; Serial always gets the message).
//
// Include this header instead of calling Serial.printf directly when you
// want the output visible over BLE without a USB cable attached.

#include <Arduino.h>
#include <stdarg.h>
#include "ble_remote.h"

inline void ble_logf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    Serial.print(buf);
    ble_remote::sendLog(buf, (size_t)n);
}

#define BLOG(fmt, ...) ble_logf(fmt, ##__VA_ARGS__)
