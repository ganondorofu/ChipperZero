#include "wifi_sniffer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/task.h>

#include "../hal/encoder.h"

WifiSnifferModule g_wifiSniffer;

// ---- Promiscuous callback ---------------------------------------------------

static void snifferCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    switch (type) {
        case WIFI_PKT_MGMT: g_wifiSniffer.addMgmt(); break;
        case WIFI_PKT_DATA: g_wifiSniffer.addData(); break;
        case WIFI_PKT_CTRL: g_wifiSniffer.addCtrl(); break;
        default: break;
    }
}

// ---- Task -------------------------------------------------------------------

static void snifferTask(void* arg) {
    WifiSnifferModule* self = reinterpret_cast<WifiSnifferModule*>(arg);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(snifferCb);

    uint8_t ch = 1;
    uint32_t lastHop = millis();

    while (self->isRunning()) {
        // Hop channel every 200ms
        if (millis() - lastHop >= 200) {
            ch = (ch % 13) + 1;
            self->channel_ = ch;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            lastHop = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- IModule ----------------------------------------------------------------

void WifiSnifferModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    mgmt_ = 0; data_ = 0; ctrl_ = 0;
    channel_ = 1;
    xTaskCreatePinnedToCore(snifferTask, "sniffer", 4096, this, 1, &task_, 0);
}

void WifiSnifferModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 60 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void WifiSnifferModule::onEvent(uint8_t ev) {
    // Manual channel control with LEFT/RIGHT
    if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT)) {
        channel_ = (channel_ % 13) + 1;
        esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE);
    } else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT)) {
        channel_ = (channel_ == 1) ? 13 : channel_ - 1;
        esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE);
    }
}

void WifiSnifferModule::fillStats(char* buf, size_t len) {
    snprintf(buf, len, "Ch%u Mgmt:%lu\nData:%lu Ctrl:%lu",
             channel_,
             (unsigned long)mgmt_.load(),
             (unsigned long)data_.load(),
             (unsigned long)ctrl_.load());
}
