#include "wifi_scan.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

WifiScanModule g_wifiScan;

namespace {

struct APInfo {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    bool    open;
};

static APInfo  s_aps[32];
static uint8_t s_apCount  = 0;
static uint8_t s_scroll   = 0;
static bool    s_scanning = false;

void wifiScanTask(void* arg) {
    WifiScanModule* self = reinterpret_cast<WifiScanModule*>(arg);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    while (self->isRunning()) {
        if (!s_scanning) {
            s_scanning = true;
            WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
        }

        int n = WiFi.scanComplete();
        if (n >= 0) {
            uint8_t cnt = (n > 32) ? 32 : (uint8_t)n;
            for (uint8_t i = 0; i < cnt; i++) {
                strncpy(s_aps[i].ssid, WiFi.SSID(i).c_str(), 32);
                s_aps[i].ssid[32] = '\0';
                s_aps[i].rssi    = (int8_t)WiFi.RSSI(i);
                s_aps[i].channel = (uint8_t)WiFi.channel(i);
                s_aps[i].open    = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
            }
            s_apCount = cnt;
            if (s_scroll >= s_apCount && s_apCount > 0) s_scroll = s_apCount - 1;
            WiFi.scanDelete();
            s_scanning = false;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

void WifiScanModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    s_apCount = 0;
    s_scroll  = 0;
    s_scanning = false;
    xTaskCreatePinnedToCore(wifiScanTask, "wifi_scan", 4096, this, 1, &task_, 0);
}

void WifiScanModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 60 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void WifiScanModule::onEvent(uint8_t ev) {
    if (s_apCount == 0) return;
    // EVENT_RIGHT=2, EVENT_LEFT=1
    if (ev == 2 && s_scroll + 1 < s_apCount) s_scroll++;
    else if (ev == 1 && s_scroll > 0)        s_scroll--;
}

void WifiScanModule::fillStats(char* buf, size_t len) {
    if (s_apCount == 0) {
        snprintf(buf, len, "Scanning...\n");
        return;
    }
    const APInfo& ap = s_aps[s_scroll];
    char ssid[14];
    strncpy(ssid, ap.ssid, 13);
    ssid[13] = '\0';
    // Line1: index + RSSI + channel
    // Line2: SSID  (* if encrypted)
    snprintf(buf, len, "%u/%u Ch%u %ddBm\n%s%s",
             s_scroll + 1, s_apCount,
             ap.channel, ap.rssi,
             ssid,
             ap.open ? "" : "*");
}
