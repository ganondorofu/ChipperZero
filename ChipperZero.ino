// ChipperZero - ESP32 handheld firmware
// See docs/architecture.md for the runtime model and docs/wiring.md for pinout.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "src/hal/ble_remote.h"
#include "src/hal/nrf_radio.h"
#include "src/hal/display.h"
#include "src/hal/encoder.h"
#include "src/hal/power.h"
#include "src/modules/ble_spam.h"
#include "src/modules/ir.h"
#include "src/modules/nfc.h"
#include "src/modules/nrf_spam.h"
#include "src/modules/nrf_ble_spam.h"
#include "src/modules/storage.h"
#include "src/modules/wifi_scan.h"
#include "src/modules/wifi_manager.h"
#include "src/ui/menu.h"

// Shared VSPI mutex. NRF24L01 and SD both live on VSPI; every driver access
// must take this mutex (xSemaphoreTake / xSemaphoreGive) to prevent contention.
SemaphoreHandle_t spi_mutex = nullptr;

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println("ChipperZero v0.1 starting...");

    spi_mutex = xSemaphoreCreateMutex();

    power::begin();

    if (display::begin()) {
        Serial.println("Display init OK");
    } else {
        Serial.println("Display init FAILED");
    }

    encoder::begin();
    Serial.println("Encoder init OK");

    if (ble_remote::begin("ChipperZero")) {
        ble_remote::start();  // auto-advertise per docs/ble_remote.md
        Serial.println("BLE Remote advertising");
    } else {
        Serial.println("BLE Remote init FAILED");
    }

    // Module init() is allowed to fail (returns false → greyed out in menu).
    g_nrfSpam.init();
    g_nrfBleSpam.init();
    g_bleSpam.init();
    g_nfc.init();
    g_ir.init();
    g_wifiScan.init();
    g_storage.init();
    g_wifiManager.init();
    Serial.println("NRF24 init OK");

    menu::begin();
    Serial.println("Menu ready");
}

void loop() {
    menu::update();
    display::flush();
    g_wifiManager.handleClient();
}
