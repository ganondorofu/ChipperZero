#include "display.h"

#include "ble_remote.h"
#include "pins.h"

namespace display {

namespace {
U8G2_SH1106_128X64_NONAME_F_HW_I2C g_u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE,
                                          PIN_I2C_SCL, PIN_I2C_SDA);
bool g_dirty = true;

// SH1106 128x64 page buffer = 16 tiles wide * 8 tiles tall * 8 bytes = 1024 bytes.
constexpr size_t kFrameBytes = 128 * 64 / 8;
}  // namespace

bool begin() {
    bool ok = g_u8g2.begin();
    g_u8g2.setBusClock(100000);
    g_u8g2.clearBuffer();
    g_u8g2.sendBuffer();
    g_dirty = false;
    return ok;
}

U8G2& u8g2() {
    return g_u8g2;
}

void markDirty() {
    g_dirty = true;
}

void flush() {
    if (!g_dirty) return;
    g_u8g2.sendBuffer();
    // Mirror the OLED contents to any connected BLE client. No-op if no
    // client is subscribed; cheap enough to do on every dirty flush.
    ble_remote::sendFrame(g_u8g2.getBufferPtr(), kFrameBytes);
    g_dirty = false;
}

}  // namespace display
