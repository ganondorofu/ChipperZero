#include "ble_scan.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/task.h>

#include <math.h>

#include "../hal/encoder.h"

BleScanModule g_bleScan;

// ---- RSSI utilities ---------------------------------------------------------

static float rssiToMeters(int8_t rssi) {
    // Free-space path loss model: d = 10^((txPower - RSSI) / (10 * n))
    // txPower = RSSI at 1m = -59 dBm (BLE typical), n = 2.0
    if (rssi >= 0) return 0.0f;
    return powf(10.0f, (-59.0f - (float)rssi) / 20.0f);
}

// 8-segment ASCII bar based on RSSI (-100...-30 dBm)
static void rssiBar(int8_t rssi, char* out) {
    int v = rssi + 100;           // 0 (-100dBm) … 70 (-30dBm)
    if (v < 0) v = 0;
    if (v > 70) v = 70;
    uint8_t bars = (uint8_t)(v * 8 / 70);  // 0-8
    for (uint8_t i = 0; i < 8; i++) out[i] = (i < bars) ? '|' : ' ';
    out[8] = '\0';
}

// ---- Device list ------------------------------------------------------------

struct BleDevice {
    char   mac[18];
    char   name[18];
    int8_t rssi;
};

static BleDevice    s_devs[24];
static uint8_t      s_devCount = 0;
static portMUX_TYPE s_devMux   = portMUX_INITIALIZER_UNLOCKED;

// ---- Scan callback ----------------------------------------------------------

class BleScanner : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        std::string macStr = dev->getAddress().toString();
        const char* mac = macStr.c_str();
        portENTER_CRITICAL(&s_devMux);
        for (uint8_t i = 0; i < s_devCount; i++) {
            if (strncmp(s_devs[i].mac, mac, 17) == 0) {
                s_devs[i].rssi = (int8_t)dev->getRSSI();
                portEXIT_CRITICAL(&s_devMux);
                return;
            }
        }
        if (s_devCount < 24) {
            strncpy(s_devs[s_devCount].mac, mac, 17);
            std::string nameStr = dev->haveName() ? dev->getName() : "";
            const char* name = nameStr.c_str();
            strncpy(s_devs[s_devCount].name, name[0] ? name : mac, 17);
            s_devs[s_devCount].rssi = (int8_t)dev->getRSSI();
            s_devCount++;
        }
        portEXIT_CRITICAL(&s_devMux);
    }
};

static BleScanner s_scanCb;

// ---- helpers ----------------------------------------------------------------

void BleScanModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

// ---- Task -------------------------------------------------------------------

static void bleScanTask(void* arg) {
    BleScanModule* self = reinterpret_cast<BleScanModule*>(arg);

    portENTER_CRITICAL(&s_devMux);
    s_devCount = 0;
    portEXIT_CRITICAL(&s_devMux);

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&s_scanCb, false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(90);
    pScan->start(0, false);  // continuous, non-blocking

    char buf[48];
    while (self->isRunning()) {
        uint8_t cnt, idx;
        portENTER_CRITICAL(&s_devMux);
        cnt = s_devCount;
        idx = self->scroll_ < cnt ? self->scroll_ : (cnt > 0 ? cnt - 1 : 0);
        portEXIT_CRITICAL(&s_devMux);

        if (cnt == 0) {
            self->setStatus("Scanning...\n(no devices yet)");
        } else {
            portENTER_CRITICAL(&s_devMux);
            int8_t  rssi = s_devs[idx].rssi;
            char    name[18];
            strncpy(name, s_devs[idx].name, 17);
            portEXIT_CRITICAL(&s_devMux);

            char bar[9];
            rssiBar(rssi, bar);

            float dist = rssiToMeters(rssi);
            char distStr[8];
            if (dist < 0.5f)        snprintf(distStr, sizeof(distStr), "<0.5m");
            else if (dist < 100.0f) snprintf(distStr, sizeof(distStr), "~%.1fm", dist);
            else                    snprintf(distStr, sizeof(distStr), ">100m");

            // Line1: bar + RSSI  Line2: distance + name  (idx/cnt suffix on line2)
            snprintf(buf, sizeof(buf), "[%s]%4ddBm\n%s %u/%u %.12s",
                     bar, rssi, distStr, idx + 1, cnt, name);
            self->setStatus(buf);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    NimBLEDevice::getScan()->stop();
    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- IModule ----------------------------------------------------------------

void BleScanModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    scroll_ = 0;
    xTaskCreatePinnedToCore(bleScanTask, "ble_scan", 4096, this, 1, &task_, 0);
}

void BleScanModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 60 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void BleScanModule::onEvent(uint8_t ev) {
    uint8_t cnt;
    portENTER_CRITICAL(&s_devMux);
    cnt = s_devCount;
    portEXIT_CRITICAL(&s_devMux);
    if (cnt == 0) return;

    if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT))
        scroll_ = (scroll_ + 1) % cnt;
    else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT))
        scroll_ = (scroll_ + cnt - 1) % cnt;
}

void BleScanModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
