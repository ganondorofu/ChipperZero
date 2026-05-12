#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "hal/ble_remote.h"
#include "hal/nrf_radio.h"
#include "hal/display.h"
#include "hal/encoder.h"
#include "hal/power.h"
#include "modules/ble_spam.h"
#include "modules/ir.h"
#include "modules/nfc.h"
#include "modules/nrf_spam.h"
#include "modules/nrf_ble_spam.h"
#include "modules/nrf_jammer.h"
#include "modules/storage.h"
#include "modules/wifi_scan.h"
#include "modules/wifi_beacon_clone.h"
#include "modules/wifi_evil_portal.h"
#include "modules/wifi_spectrum.h"
#include "modules/nrf_mousejack.h"
#include "ui/menu.h"

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
        ble_remote::start();
        Serial.println("BLE Remote advertising");
    } else {
        Serial.println("BLE Remote init FAILED");
    }

    g_nrfSpam.init();
    g_nrfBleSpam.init();
    g_nrfJammer.init();
    g_bleSpam.init();
    g_nfc.init();
    g_ir.init();
    g_wifiScan.init();
    g_wifiBeaconClone.init();
    g_storage.init();
    g_wifiEvilPortal.init();
    g_wifiSpectrum.init();
    g_nrfMousejack.init();
    Serial.println("NRF24 init OK");

    menu::begin();
    Serial.println("Menu ready");
}

void loop() {
    menu::update();
    display::flush();
}
