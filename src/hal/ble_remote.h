#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ble_remote {

// Initialise the NimBLE stack and create the ChipperZero remote GATT service.
// Call once from setup() AFTER encoder::begin() (so injectEvent has a queue).
// Returns false if NimBLE failed to initialise.
bool begin(const char* deviceName = "ChipperZero");

// Start advertising. Idempotent.
void start();

// Stop advertising and disconnect any connected client. Idempotent.
void stop();

// Toggle helper used by the menu.
void setEnabled(bool on);

bool isEnabled();
bool isConnected();

// Send the OLED framebuffer to the connected client as a sequence of
// notifications on the framebuffer characteristic. Safe to call when
// not connected (no-op). buf is the SH1106 page-format buffer (1024 bytes
// for 128x64). Frames are split into 240-byte chunks; chunk 0 starts a
// new frame on the receiver and the final chunk completes it.
void sendFrame(const uint8_t* buf, size_t len);

// Send a UTF-8 log message (up to 244 bytes) to the connected client via
// the log characteristic (UUID c4b1e004). No-op when not connected.
void sendLog(const char* msg, size_t len);

// Tear down and re-initialise the NimBLE stack. Call after any module that
// called NimBLEDevice::deinit() so ble_remote can reclaim the stack.
void reinit(const char* deviceName = "ChipperZero");

}  // namespace ble_remote
