#include "wifi_beacon_clone.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/task.h>

#include "../hal/encoder.h"

BeaconCloneModule g_wifiBeaconClone;

namespace {

struct CloneAP {
    char    ssid[33];
    int32_t rssi;
    uint8_t channel;
};

static CloneAP  s_aps[24];
static uint8_t  s_apCount = 0;
static volatile bool s_confirmed = false;

static void randMac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = esp_random() & 0xFF;
    mac[0] &= 0xFE;
    mac[0] |= 0x02;
}

static void sendBeacon(const char* ssid, uint8_t channel, uint8_t* bssid) {
    uint8_t ssidLen = (uint8_t)strlen(ssid);
    uint8_t frame[128];
    uint8_t i = 0;

    frame[i++] = 0x80; frame[i++] = 0x00;
    frame[i++] = 0x00; frame[i++] = 0x00;
    frame[i++] = 0xFF; frame[i++] = 0xFF; frame[i++] = 0xFF;
    frame[i++] = 0xFF; frame[i++] = 0xFF; frame[i++] = 0xFF;
    memcpy(&frame[i], bssid, 6); i += 6;
    memcpy(&frame[i], bssid, 6); i += 6;
    frame[i++] = 0x00; frame[i++] = 0x00;

    uint64_t ts = esp_timer_get_time();
    memcpy(&frame[i], &ts, 8); i += 8;
    frame[i++] = 0x64; frame[i++] = 0x00;
    frame[i++] = 0x11; frame[i++] = 0x04;

    frame[i++] = 0x00; frame[i++] = ssidLen;
    memcpy(&frame[i], ssid, ssidLen); i += ssidLen;

    frame[i++] = 0x01; frame[i++] = 0x08;
    frame[i++] = 0x82; frame[i++] = 0x84; frame[i++] = 0x8B; frame[i++] = 0x96;
    frame[i++] = 0x24; frame[i++] = 0x30; frame[i++] = 0x48; frame[i++] = 0x6C;

    frame[i++] = 0x03; frame[i++] = 0x01; frame[i++] = channel;

    esp_wifi_80211_tx(WIFI_IF_AP, frame, i, false);
}

static void beaconCloneTask(void* arg) {
    BeaconCloneModule* self = reinterpret_cast<BeaconCloneModule*>(arg);
    char buf[48];

    // Phase 1: scan
    self->setStatus("Scanning APs...");
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, true);
    s_apCount = 0;
    if (n > 0) {
        uint8_t cnt = n > 24 ? 24 : (uint8_t)n;
        for (uint8_t i = 0; i < cnt; i++) {
            strncpy(s_aps[i].ssid, WiFi.SSID(i).c_str(), 32);
            s_aps[i].ssid[32] = '\0';
            s_aps[i].rssi    = WiFi.RSSI(i);
            s_aps[i].channel = (uint8_t)WiFi.channel(i);
        }
        s_apCount = cnt;
    }
    WiFi.scanDelete();

    if (s_apCount == 0) {
        self->setStatus("No APs found");
        vTaskDelay(pdMS_TO_TICKS(1500));
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    // Phase 2: select
    self->state_ = CloneState::SELECTING;
    while (self->isRunning() && !s_confirmed) {
        uint8_t idx = self->scroll_ < s_apCount ? self->scroll_ : s_apCount - 1;
        snprintf(buf, sizeof(buf), "%u/%u %ddBm\nOK: %.22s",
                 idx + 1, s_apCount, s_aps[idx].rssi, s_aps[idx].ssid);
        self->setStatus(buf);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (!self->isRunning()) {
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    // Phase 3: spam clone beacons
    uint8_t sel = self->scroll_ < s_apCount ? self->scroll_ : 0;
    const char* ssid = s_aps[sel].ssid;
    uint8_t     ch   = s_aps[sel].channel;

    self->state_ = CloneState::CLONING;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, nullptr, ch, 0, 1);

    uint32_t sent = 0;
    while (self->isRunning()) {
        uint8_t bssid[6];
        randMac(bssid);
        for (int r = 0; r < 3; r++)
            sendBeacon(ssid, ch, bssid);
        sent++;
        snprintf(buf, sizeof(buf), "%.20s\n%lu frames", ssid, (unsigned long)sent);
        self->setStatus(buf);
        vTaskDelay(10);
    }

    WiFi.mode(WIFI_OFF);
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

void BeaconCloneModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

void BeaconCloneModule::confirmSelect() {
    s_confirmed = true;
}

void BeaconCloneModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    scroll_     = 0;
    s_confirmed = false;
    sent_.store(0);
    state_      = CloneState::SCANNING;
    status_[0]  = '\0';
    xTaskCreatePinnedToCore(beaconCloneTask, "beacon_clone", 4096, this, 1, &task_, 0);
}

void BeaconCloneModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 100 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void BeaconCloneModule::onEvent(uint8_t ev) {
    if (state_ != CloneState::SELECTING || s_apCount == 0) return;
    if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT))
        scroll_ = (scroll_ + 1) % s_apCount;
    else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT))
        scroll_ = (scroll_ + s_apCount - 1) % s_apCount;
    else if (ev == static_cast<uint8_t>(encoder::EVENT_OK))
        confirmSelect();
}

void BeaconCloneModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
