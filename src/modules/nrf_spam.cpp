#include "nrf_spam.h"

#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../hal/pins.h"
#include "../hal/ble_log.h"
#include "../hal/nrf_radio.h"

NrfSpamModule g_nrfSpam;

namespace {

void spamTask(void* arg) {
    NrfSpamModule* self = reinterpret_cast<NrfSpamModule*>(arg);

    uint8_t payload[32];
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)esp_random();

    if (self->hasChip() && nrfLockSpi()) {
        g_nrf_radio.powerUp();
        g_nrf_radio.stopListening();
        nrfUnlockSpi();
    }

    BLOG("[nrf_spam] task started\n");
    uint32_t txCount = 0;
    uint32_t lastReport = millis();
    while (self->isRunning()) {
        if (!self->hasChip()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (nrfLockSpi(pdMS_TO_TICKS(20))) {
            // Hop several channels per mutex window to keep noise dense.
            for (int k = 0; k < 8 && self->isRunning(); ++k) {
                uint8_t channel = (uint8_t)(esp_random() & 0x7F);  // 0..127 = 2400..2527 MHz
                g_nrf_radio.setChannel(channel);
                for (size_t i = 0; i < sizeof(payload); ++i)
                    payload[i] = (uint8_t)esp_random();
                g_nrf_radio.writeFast(payload, sizeof(payload));
                g_nrf_radio.txStandBy(250);  // 250ms timeout prevents hang on TX fail
                txCount++;
            }
            nrfUnlockSpi();
        }
        if (millis() - lastReport >= 1000) {
            BLOG("[nrf_spam] tx/sec=%lu chip=%d\n", (unsigned long)txCount, (int)self->hasChip());
            txCount = 0;
            lastReport = millis();
        }
        vTaskDelay(1);  // ~1ms yield so menu / BLE keep running
    }

    if (self->hasChip() && nrfLockSpi()) {
        g_nrf_radio.powerDown();
        nrfUnlockSpi();
    }
    BLOG("[nrf_spam] task exiting\n");
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

bool NrfSpamModule::init() {
    if (!nrfLockSpi(pdMS_TO_TICKS(200))) {
        BLOG("[nrf_spam] init: spi_mutex timeout\n");
        available_ = false;
        return false;
    }
    pinMode(PIN_NRF_CE, OUTPUT);
    pinMode(PIN_NRF_CSN, OUTPUT);
    digitalWrite(PIN_NRF_CE, LOW);
    digitalWrite(PIN_NRF_CSN, HIGH);
    delay(10);

    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    bool begun = g_nrf_radio.begin();
    bool chip = begun && g_nrf_radio.isChipConnected();

    // Always read raw STATUS register (NOP=0xFF) for diagnosis.
    // 0x0E = chip present  0xFF = MISO floating  0x00 = MOSI/SCK issue
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_NRF_CSN, LOW);
    uint8_t rawStatus = SPI.transfer(0xFF);
    digitalWrite(PIN_NRF_CSN, HIGH);
    SPI.endTransaction();

    // One combined line so BLE can't drop part of the diagnostic.
    BLOG("[nrf_spam] init: begin=%d chip=%d spiSTATUS=0x%02X\n", begun, chip, rawStatus);

    if (begun) {
        g_nrf_radio.setAutoAck(false);
        g_nrf_radio.stopListening();
        g_nrf_radio.setRetries(0, 0);
        g_nrf_radio.setPayloadSize(32);
        g_nrf_radio.setDataRate(RF24_2MBPS);
        g_nrf_radio.setPALevel(RF24_PA_MAX);
        g_nrf_radio.setCRCLength(RF24_CRC_DISABLED);
        g_nrf_radio.openWritingPipe((uint64_t)0xE7E7E7E7E7ULL);
        g_nrf_radio.powerDown();
    }
    nrfUnlockSpi();
    available_ = true;
    detected_ = begun;
    BLOG("[nrf_spam] init: chip %s\n", detected_ ? "OK" : "NOT DETECTED");
    return true;
}

bool NrfSpamModule::isAvailable() { return true; }
bool NrfSpamModule::isRunning() const { return running_; }

void NrfSpamModule::start() {
    if (running_.exchange(true)) return;  // atomic test-and-set; already running → bail
    if (task_ != nullptr) { running_ = false; return; }
    // Always re-init so the chip is in ESB mode regardless of prior module.
    init();
    BLOG("[nrf_spam] start (chip=%s)\n", detected_ ? "YES" : "NO");
    xTaskCreatePinnedToCore(spamTask, "nrf_spam", 8192, this, 1, &task_, 0);
}

void NrfSpamModule::stop() {
    if (!running_) return;
    BLOG("[nrf_spam] stop\n");
    running_ = false;
    // Wait up to 500ms for the task to clean up and self-delete.
    for (int i = 0; i < 50 && task_; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
        eTaskState st = eTaskGetState(task_);
        if (st == eDeleted || st == eInvalid) break;
    }
    task_ = nullptr;
}
