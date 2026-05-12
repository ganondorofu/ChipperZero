#include "wifi_evil_twin.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/task.h>

#include "../hal/encoder.h"

WifiEvilTwinModule g_wifiEvilTwin;

// ---- AP list ----------------------------------------------------------------

struct APEntry {
    char    ssid[33];
    int32_t rssi;
    uint8_t channel;
    bool    open;
};

static APEntry s_aps[24];
static uint8_t s_apCount   = 0;
static char    s_targetSSID[33] = "";
static volatile bool s_doTwin = false;

// ---- helpers ----------------------------------------------------------------

void WifiEvilTwinModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

void WifiEvilTwinModule::startTwin() {
    s_doTwin = true;
}

// ---- Task -------------------------------------------------------------------

static void evilTwinTask(void* arg) {
    WifiEvilTwinModule* self = reinterpret_cast<WifiEvilTwinModule*>(arg);

    // Phase 1: scan
    self->setStatus("Scanning APs...");
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, true);
    s_apCount = 0;
    if (n > 0) {
        uint8_t cnt = n > 24 ? 24 : (uint8_t)n;
        for (uint8_t i = 0; i < cnt; i++) {
            strncpy(s_aps[i].ssid, WiFi.SSID(i).c_str(), 32);
            s_aps[i].rssi    = WiFi.RSSI(i);
            s_aps[i].channel = (uint8_t)WiFi.channel(i);
            s_aps[i].open    = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        }
        s_apCount = cnt;
    }
    WiFi.scanDelete();

    char buf[48];

    if (s_apCount == 0) {
        self->setStatus("No APs found");
        vTaskDelay(pdMS_TO_TICKS(1500));
        WiFi.mode(WIFI_OFF);
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    if (self->autoMode_) {
        // pick the strongest AP automatically
        uint8_t best = 0;
        for (uint8_t i = 1; i < s_apCount; i++)
            if (s_aps[i].rssi > s_aps[best].rssi) best = i;
        self->scroll_ = best;
        s_doTwin = true;
    } else {
        // Manual: let user scroll and confirm
        while (self->isRunning() && !s_doTwin) {
            uint8_t idx = self->scroll_ < s_apCount ? self->scroll_ : s_apCount - 1;
            snprintf(buf, sizeof(buf), "%u/%u %ddBm Ch%u\n>%.28s",
                     idx + 1, s_apCount, s_aps[idx].rssi, s_aps[idx].channel,
                     s_aps[idx].ssid);
            self->setStatus(buf);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    if (!self->isRunning()) {
        WiFi.mode(WIFI_OFF);
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    // Phase 2: start evil twin
    uint8_t idx = self->scroll_ < s_apCount ? self->scroll_ : 0;
    strncpy(s_targetSSID, s_aps[idx].ssid, 32);
    self->state_ = EvilTwinState::TWINNING;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(s_targetSSID, nullptr, s_aps[idx].channel, 0, 8);

    snprintf(buf, sizeof(buf), "Twin: %.20s\nClients: 0", s_targetSSID);
    self->setStatus(buf);

    while (self->isRunning()) {
        uint8_t clients = WiFi.softAPgetStationNum();
        snprintf(buf, sizeof(buf), "Twin: %.20s\nClients: %u", s_targetSSID, clients);
        self->setStatus(buf);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- IModule ----------------------------------------------------------------

void WifiEvilTwinModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    scroll_  = 0;
    s_doTwin = false;
    state_   = EvilTwinState::SCANNING;
    xTaskCreatePinnedToCore(evilTwinTask, "evil_twin", 4096, this, 1, &task_, 0);
}

void WifiEvilTwinModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 100 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void WifiEvilTwinModule::onEvent(uint8_t ev) {
    if (state_ == EvilTwinState::SCANNING) {
        if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT) && s_apCount > 0)
            scroll_ = (scroll_ + 1) % s_apCount;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT) && s_apCount > 0)
            scroll_ = (scroll_ + s_apCount - 1) % s_apCount;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_OK) && s_apCount > 0)
            startTwin();
    }
}

void WifiEvilTwinModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
