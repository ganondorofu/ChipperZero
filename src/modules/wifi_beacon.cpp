#include "wifi_beacon.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

BeaconSpamModule g_wifiBeacon;

namespace {

static void randMac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = esp_random() & 0xFF;
    mac[0] &= 0xFE;  // unicast
    mac[0] |= 0x02;  // locally administered
}

static void randSSID(char* buf, uint8_t len) {
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    uint8_t n = 6 + (esp_random() % 7);  // 6-12 chars
    if (n >= len) n = len - 1;
    for (uint8_t i = 0; i < n; i++) buf[i] = kChars[esp_random() % (sizeof(kChars) - 1)];
    buf[n] = '\0';
}

static void sendBeacon(const char* ssid, uint8_t channel, uint8_t* bssid) {
    uint8_t ssidLen = strlen(ssid);
    uint8_t frame[128];
    uint8_t i = 0;

    // MAC header
    frame[i++] = 0x80; frame[i++] = 0x00;  // FC: Beacon
    frame[i++] = 0x00; frame[i++] = 0x00;  // Duration
    frame[i++] = 0xFF; frame[i++] = 0xFF; frame[i++] = 0xFF;
    frame[i++] = 0xFF; frame[i++] = 0xFF; frame[i++] = 0xFF;  // DA: broadcast
    memcpy(&frame[i], bssid, 6); i += 6;   // SA
    memcpy(&frame[i], bssid, 6); i += 6;   // BSSID
    frame[i++] = 0x00; frame[i++] = 0x00;  // SeqCtrl

    // Fixed fields
    uint64_t ts = esp_timer_get_time();
    memcpy(&frame[i], &ts, 8); i += 8;     // Timestamp
    frame[i++] = 0x64; frame[i++] = 0x00;  // Beacon interval: 100 TU
    frame[i++] = 0x11; frame[i++] = 0x04;  // Capability: ESS + Short Preamble

    // SSID IE
    frame[i++] = 0x00; frame[i++] = ssidLen;
    memcpy(&frame[i], ssid, ssidLen); i += ssidLen;

    // Supported rates IE
    frame[i++] = 0x01; frame[i++] = 0x08;
    frame[i++] = 0x82; frame[i++] = 0x84; frame[i++] = 0x8B; frame[i++] = 0x96;
    frame[i++] = 0x24; frame[i++] = 0x30; frame[i++] = 0x48; frame[i++] = 0x6C;

    // DS Parameter Set (channel)
    frame[i++] = 0x03; frame[i++] = 0x01; frame[i++] = channel;

    esp_wifi_80211_tx(WIFI_IF_AP, frame, i, false);
}

void beaconTask(void* arg) {
    BeaconSpamModule* self = reinterpret_cast<BeaconSpamModule*>(arg);

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ChipperZero", nullptr, 1, 0, 1);

    uint32_t count = 0;
    uint32_t lastReport = millis();
    uint8_t  ch = 1;

    while (self->isRunning()) {
        char ssid[13];
        uint8_t bssid[6];
        randSSID(ssid, sizeof(ssid));
        randMac(bssid);

        for (int rep = 0; rep < 3 && self->isRunning(); rep++) {
            sendBeacon(ssid, ch, bssid);
            count++;
        }
        ch = (ch % 13) + 1;

        if (millis() - lastReport >= 1000) {
            self->setRate(count);
            count = 0;
            lastReport = millis();
        }
        vTaskDelay(10);
    }

    WiFi.mode(WIFI_OFF);
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

void BeaconSpamModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    rate_ = 0;
    xTaskCreatePinnedToCore(beaconTask, "wifi_beacon", 4096, this, 1, &task_, 0);
}

void BeaconSpamModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 60 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void BeaconSpamModule::fillStats(char* buf, size_t len) {
    snprintf(buf, len, "%lu beacon/s\n", (unsigned long)rate_.load());
}
