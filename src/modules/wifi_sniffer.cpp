#include "wifi_sniffer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/task.h>

#include "../hal/encoder.h"

WifiSnifferModule g_wifiSniffer;

// ---- Client MAC storage (static members) ------------------------------------

uint8_t WifiSnifferModule::s_clientMacs[8][6] = {};
uint8_t WifiSnifferModule::s_clientCount = 0;

void WifiSnifferModule::addClientMac(const uint8_t* mac) {
    // Ignore multicast/broadcast (LSB of first byte set)
    if (mac[0] & 0x01) return;
    for (uint8_t i = 0; i < s_clientCount; i++) {
        if (memcmp(s_clientMacs[i], mac, 6) == 0) return;
    }
    if (s_clientCount < 8) {
        memcpy(s_clientMacs[s_clientCount], mac, 6);
        s_clientCount++;
    }
}

uint8_t WifiSnifferModule::getClientCount() { return s_clientCount; }

bool WifiSnifferModule::getClientMac(uint8_t i, uint8_t mac[6]) {
    if (i >= s_clientCount) return false;
    memcpy(mac, s_clientMacs[i], 6);
    return true;
}

// ---- Promiscuous callback ---------------------------------------------------

static void snifferCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* frame = pkt->payload;

    switch (type) {
        case WIFI_PKT_MGMT: g_wifiSniffer.addMgmt(); break;
        case WIFI_PKT_DATA: {
            g_wifiSniffer.addData();
            // 802.11 data frame: frame control is bytes 0-1
            // fc[1] bit0=ToDS, bit1=FromDS
            uint8_t fc1 = frame[1];
            bool toDS   = fc1 & 0x01;
            bool fromDS = fc1 & 0x02;
            if (toDS && !fromDS) {
                // Client→AP: Addr2 (bytes 10–15) = client MAC
                const uint8_t* client = frame + 10;
                WifiSnifferModule::addClientMac(client);
            }
            break;
        }
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
    uint8_t cc = s_clientCount;
    if (cc > 0) {
        // Show first client MAC on second line
        uint8_t* m = s_clientMacs[0];
        snprintf(buf, len, "Ch:%u M:%lu D:%lu\nC:%u %02X:%02X:%02X:%02X:%02X:%02X",
                 channel_,
                 (unsigned long)mgmt_.load(),
                 (unsigned long)data_.load(),
                 cc,
                 m[0], m[1], m[2], m[3], m[4], m[5]);
    } else {
        snprintf(buf, len, "Ch:%u  M:%lu D:%lu\nClients: 0",
                 channel_,
                 (unsigned long)mgmt_.load(),
                 (unsigned long)data_.load());
    }
}
