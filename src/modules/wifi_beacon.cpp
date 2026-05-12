#include "wifi_beacon.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../hal/encoder.h"

BeaconSpamModule g_wifiBeacon;

// ---- NVS persistence --------------------------------------------------------

bool BeaconSpamModule::init() {
    Preferences prefs;
    prefs.begin("beacon", true);
    String saved = prefs.getString("ssid", "");
    prefs.end();
    if (saved.length() > 0) {
        strncpy(customSSID_, saved.c_str(), sizeof(customSSID_) - 1);
        customSSID_[sizeof(customSSID_) - 1] = '\0';
        useCustom_ = true;
    }
    return true;
}

void BeaconSpamModule::setCustomSSID(const char* s) {
    strncpy(customSSID_, s, sizeof(customSSID_) - 1);
    customSSID_[sizeof(customSSID_) - 1] = '\0';
    useCustom_ = (customSSID_[0] != '\0');
    Preferences prefs;
    prefs.begin("beacon", false);
    prefs.putString("ssid", useCustom_ ? customSSID_ : "");
    prefs.end();
}

namespace {

const char* kPatternNames[] = {
    "Random", "NETGEAR", "TP-Link", "iPhone",
    "AndroidAP", "FreeWiFi", "xfinity", "DIRECT",
};
constexpr uint8_t kPatternCount = 8;

static const uint16_t kCountPresets[] = { 0, 10, 50, 100 };  // 0 = unlimited
constexpr uint8_t kCountPresetCount = 4;

static void randMac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = esp_random() & 0xFF;
    mac[0] &= 0xFE;
    mac[0] |= 0x02;
}

static void makeSSID(char* buf, size_t len, uint8_t pattern) {
    static const char kAlpha[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    uint8_t rlen = 4 + (esp_random() % 5);

    switch (pattern) {
        case 1: {
            uint8_t n = snprintf(buf, len, "NETGEAR_%02X", (unsigned)(esp_random() & 0xFF));
            buf[n < len ? n : len-1] = '\0';
            return;
        }
        case 2: {
            uint8_t n = snprintf(buf, len, "TP-Link_%02X%02X",
                (unsigned)(esp_random()&0xFF), (unsigned)(esp_random()&0xFF));
            buf[n < len ? n : len-1] = '\0';
            return;
        }
        case 3: {
            uint8_t n = snprintf(buf, len, "iPhone %u", (unsigned)(esp_random() % 100));
            buf[n < len ? n : len-1] = '\0';
            return;
        }
        case 4: {
            uint8_t n = snprintf(buf, len, "AndroidAP_%04X", (unsigned)(esp_random() & 0xFFFF));
            buf[n < len ? n : len-1] = '\0';
            return;
        }
        case 5: {
            static const char* freeNames[] = {
                "Free WiFi", "FREE_WIFI", "FreeWifi_Guest",
                "Public_WiFi", "OpenWifi", "Guest_Network",
            };
            snprintf(buf, len, "%s", freeNames[esp_random() % 6]);
            return;
        }
        case 6: {
            uint8_t n = snprintf(buf, len, "xfinitywifi%04X", (unsigned)(esp_random() & 0xFFFF));
            buf[n < len ? n : len-1] = '\0';
            return;
        }
        case 7: {
            uint8_t n = snprintf(buf, len, "DIRECT-%02X-%s",
                (unsigned)(esp_random()&0xFF),
                (esp_random()&1) ? "HP" : "EPSON");
            buf[n < len ? n : len-1] = '\0';
            return;
        }
        default:  // 0: random
            if (rlen >= len) rlen = len - 1;
            for (uint8_t i = 0; i < rlen; i++)
                buf[i] = kAlpha[esp_random() % (sizeof(kAlpha) - 1)];
            buf[rlen] = '\0';
            return;
    }
}

static void sendBeacon(const char* ssid, uint8_t channel, uint8_t* bssid) {
    uint8_t ssidLen = strlen(ssid);
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

void beaconTask(void* arg) {
    BeaconSpamModule* self = reinterpret_cast<BeaconSpamModule*>(arg);

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ChipperZero", nullptr, 1, 0, 1);

    uint16_t limit  = self->getCount();
    uint32_t rateCount = 0;
    uint32_t lastReport = millis();
    uint8_t  ch = 1;

    self->sent_.store(0);

    while (self->isRunning()) {
        if (limit > 0 && self->getSent() >= limit) break;

        char ssid[33];
        uint8_t bssid[6];
        if (self->useCustom()) {
            strncpy(ssid, self->customSSID(), sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
        } else {
            makeSSID(ssid, sizeof(ssid), self->getPattern());
        }
        randMac(bssid);

        for (int rep = 0; rep < 3 && self->isRunning(); rep++) {
            sendBeacon(ssid, ch, bssid);
            rateCount++;
        }
        self->incSent();
        ch = (ch % 13) + 1;

        if (millis() - lastReport >= 1000) {
            self->setRate(rateCount);
            rateCount = 0;
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
    sent_.store(0);
    rate_.store(0);
    xTaskCreatePinnedToCore(beaconTask, "wifi_beacon", 4096, this, 1, &task_, 0);
}

void BeaconSpamModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 60 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void BeaconSpamModule::onEvent(uint8_t ev) {
    if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT)) {
        uint8_t p = pattern_.load();
        pattern_.store((p + kPatternCount - 1) % kPatternCount);
    } else if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT)) {
        uint8_t p = pattern_.load();
        pattern_.store((p + 1) % kPatternCount);
    } else if (ev == static_cast<uint8_t>(encoder::EVENT_OK)) {
        // cycle count preset
        uint8_t cur = 0;
        for (uint8_t i = 0; i < kCountPresetCount; i++) {
            if (kCountPresets[i] == count_) { cur = i; break; }
        }
        count_ = kCountPresets[(cur + 1) % kCountPresetCount];
    }
}

void BeaconSpamModule::fillStats(char* buf, size_t len) {
    const char* pname = kPatternNames[pattern_.load() % kPatternCount];
    uint32_t sent = sent_.load();
    if (count_ == 0) {
        snprintf(buf, len, "%s\n%lu sent  %lu/s",
            pname, (unsigned long)sent, (unsigned long)rate_.load());
    } else {
        snprintf(buf, len, "%s\n%lu/%u  %lu/s",
            pname, (unsigned long)sent, count_, (unsigned long)rate_.load());
    }
}
