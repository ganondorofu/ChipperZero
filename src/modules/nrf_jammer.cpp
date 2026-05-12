// NRF24L01+ Bluetooth jammer.
// Transmits constant carrier on BLE advertising channels (37/38/39)
// using the NRF24L01+ CONT_WAVE feature (RF_SETUP bit 7).
// Hops 2→26→80 MHz (= BLE ch 37/38/39) at ~1ms per channel.

#include "nrf_jammer.h"

#include <Arduino.h>
#include <RF24.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../hal/nrf_radio.h"
#include "../hal/pins.h"

NrfJammerModule g_nrfJammer;

// BLE advertising channels mapped to NRF24 channel numbers
static const uint8_t kBleChans[] = { 2, 26, 80 };

static void nrfJamTask(void* arg) {
    NrfJammerModule* self = reinterpret_cast<NrfJammerModule*>(arg);

    uint8_t idx = 0;
    while (self->isRunning()) {
        if (nrfLockSpi(pdMS_TO_TICKS(10))) {
            g_nrf_radio.stopConstCarrier();
            g_nrf_radio.startConstCarrier(RF24_PA_MAX, kBleChans[idx]);
            nrfUnlockSpi();
        }
        idx = (idx + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (nrfLockSpi(pdMS_TO_TICKS(50))) {
        g_nrf_radio.stopConstCarrier();
        // Restore normal TX config for other NRF modules
        g_nrf_radio.setPALevel(RF24_PA_MAX);
        g_nrf_radio.setDataRate(RF24_1MBPS);
        nrfUnlockSpi();
    }

    self->clearTask();
    vTaskDelete(nullptr);
}

bool NrfJammerModule::init() {
    if (nrfLockSpi(pdMS_TO_TICKS(200))) {
        bool ok = g_nrf_radio.begin();
        available_.store(ok);
        nrfUnlockSpi();
    }
    return available_.load();
}

bool NrfJammerModule::isAvailable() { return available_.load(); }

void NrfJammerModule::start() {
    if (!available_.load()) return;
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    xTaskCreatePinnedToCore(nrfJamTask, "nrf_jam", 2048, this, 2, &task_, 0);
}

void NrfJammerModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 80 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void NrfJammerModule::fillStats(char* buf, size_t len) {
    snprintf(buf, len, "Jamming BLE\nCH 37/38/39");
}
