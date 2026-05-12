#pragma once
#include <stdint.h>

enum class BleSpamType : uint8_t {
    ALL = 0,
    // Apple
    APPLE,           // rotate all Apple variants
    APPLE_ACTION,    // ContinuityAction popup only
    APPLE_AIRPODS,   // NewDevicePopUp (AirPods/Beats)
    APPLE_AIRTAG,    // NewAirtagPopUp
    APPLE_NEARBY,    // NearbyAction / iOS17
    // Google
    GOOGLE,
    // Samsung
    SAMSUNG,         // rotate Watch + Buds
    SAMSUNG_WATCH,
    SAMSUNG_BUDS,
    // Microsoft
    MICROSOFT,
};
