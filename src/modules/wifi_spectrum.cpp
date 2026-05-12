#include "wifi_spectrum.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <freertos/task.h>

#include "../hal/display.h"

WifiSpectrumModule g_wifiSpectrum;

namespace {

static WifiSpectrumModule* s_self = nullptr;

static void promiscCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_self || !s_self->isRunning()) return;
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
    auto* pkt = reinterpret_cast<wifi_promiscuous_pkt_t*>(buf);
    uint8_t ch = pkt->rx_ctrl.channel;
    if (ch >= 1 && ch <= 13) {
        uint16_t v = s_self->counts[ch - 1];
        if (v < 9999) s_self->counts[ch - 1] = v + 1;
    }
}

static void spectrumTask(void* arg) {
    WifiSpectrumModule* self = reinterpret_cast<WifiSpectrumModule*>(arg);
    s_self = self;

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(promiscCb);

    while (self->isRunning()) {
        for (uint8_t ch = 1; ch <= 13 && self->isRunning(); ch++) {
            self->curChan = ch - 1;
            // Decay previous reading slightly each sweep
            uint16_t prev = self->counts[ch - 1];
            self->counts[ch - 1] = (uint16_t)(prev * 7 / 8);

            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }

    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    s_self = nullptr;
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

// ---- draw() -----------------------------------------------------------------
// Bar graph: 13 channels across 128px. Called from Core 1 (menu loop).
//
// Layout:
//   y=0-11   header bar
//   y=13-54  bars (41px max height)
//   y=55-63  channel labels (1,2..13, tiny font)

constexpr uint8_t kBarW    = 8;
constexpr uint8_t kBarGap  = 1;
constexpr uint8_t kBars    = 13;
constexpr uint8_t kStartX  = (128 - kBars * (kBarW + kBarGap) + kBarGap) / 2;
constexpr uint8_t kBarMaxH = 41;
constexpr uint8_t kBarBaseY = 54;

void WifiSpectrumModule::draw() {
    auto& g = display::u8g2();
    g.clearBuffer();

    // Header
    g.setFont(u8g2_font_6x10_tf);
    g.drawStr(0, 9, "WiFi Spectrum");
    g.drawHLine(0, 11, 128);

    // Find max count for normalization
    uint16_t maxCount = 1;
    for (uint8_t i = 0; i < kBars; i++) {
        if (counts[i] > maxCount) maxCount = counts[i];
    }

    for (uint8_t i = 0; i < kBars; i++) {
        uint8_t x = kStartX + i * (kBarW + kBarGap);
        uint8_t barH = (uint8_t)((uint32_t)counts[i] * kBarMaxH / maxCount);
        if (barH < 1 && counts[i] > 0) barH = 1;

        if (i == curChan) {
            // Currently sampled channel: outline only (scanning indicator)
            g.drawFrame(x, kBarBaseY - barH, kBarW, barH > 0 ? barH : 1);
        } else {
            if (barH > 0) g.drawBox(x, kBarBaseY - barH, kBarW, barH);
        }

        // Channel label: 1-9 single digit, 10-13 two digits
        g.setFont(u8g2_font_4x6_tf);
        char lbl[3];
        snprintf(lbl, sizeof(lbl), "%u", (unsigned)(i + 1));
        uint8_t lw = g.getStrWidth(lbl);
        g.drawStr(x + (kBarW - lw) / 2, 63, lbl);
    }

    display::markDirty();
}

// ---- IModule ----------------------------------------------------------------

void WifiSpectrumModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    for (uint8_t i = 0; i < 13; i++) counts[i] = 0;
    curChan = 0;
    xTaskCreatePinnedToCore(spectrumTask, "wifi_spectrum", 4096, this, 1, &task_, 0);
}

void WifiSpectrumModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 80 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}
