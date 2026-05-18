#include "ble_remote.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "encoder.h"
#include "../modules/wifi_manager.h"

namespace ble_remote {

// ---- ChipperZero Remote Control GATT service --------------------------------
// See docs/ble_remote.md for the full protocol specification.
//
// Service UUID:        c4b1e000-7a66-4c12-9c69-d2f8c4a3f100
// Command Char (W/WNR): c4b1e001-7a66-4c12-9c69-d2f8c4a3f100
//   Write a single byte:
//     0x01 LEFT   0x02 RIGHT   0x03 OK   0x04 BACK
// Status Char (Notify): c4b1e002-7a66-4c12-9c69-d2f8c4a3f100
//   Notifies firmware version / connection ack on subscribe.
// Framebuffer Char (Notify): c4b1e003-7a66-4c12-9c69-d2f8c4a3f100
//   Streams the SH1106 page-format buffer (1024 bytes) as 5 chunks:
//     packet[0] = chunk index (0..4); packet[1..] = up to 240 bytes of pixel data.
//   Index 0 starts a new frame; index 4 is the last chunk.

constexpr const char* kSvcUuid     = "c4b1e000-7a66-4c12-9c69-d2f8c4a3f100";
constexpr const char* kCmdUuid     = "c4b1e001-7a66-4c12-9c69-d2f8c4a3f100";
constexpr const char* kStatusUuid  = "c4b1e002-7a66-4c12-9c69-d2f8c4a3f100";
constexpr const char* kFbUuid      = "c4b1e003-7a66-4c12-9c69-d2f8c4a3f100";
constexpr const char* kLogUuid     = "c4b1e004-7a66-4c12-9c69-d2f8c4a3f100";
constexpr const char* kWifiCfgUuid = "c4b1e005-7a66-4c12-9c69-d2f8c4a3f100";

namespace {

NimBLEServer*         g_server      = nullptr;
NimBLECharacteristic* g_statusChar  = nullptr;
NimBLECharacteristic* g_fbChar      = nullptr;
NimBLECharacteristic* g_logChar     = nullptr;
NimBLECharacteristic* g_wifiCfgChar = nullptr;
bool g_initialised      = false;
bool g_enabled          = false;
bool g_connected        = false;
bool g_newConnection    = false;

encoder::InputEvent opcodeToEvent(uint8_t op) {
    switch (op) {
        case 0x01: return encoder::EVENT_LEFT;
        case 0x02: return encoder::EVENT_RIGHT;
        case 0x03: return encoder::EVENT_OK;
        case 0x04: return encoder::EVENT_BACK;
        default:   return encoder::EVENT_NONE;
    }
}

class CmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*info*/) override {
        std::string v = c->getValue();
        if (v.empty()) return;
        encoder::InputEvent ev = opcodeToEvent(static_cast<uint8_t>(v[0]));
        if (ev != encoder::EVENT_NONE) {
            encoder::injectEvent(ev);
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& /*info*/) override {
        g_connected     = true;
        g_newConnection = true;
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& /*info*/, int /*reason*/) override {
        g_connected = false;
        if (g_enabled) {
            // NimBLE stops advertising on disconnect; restart so we stay reachable.
            NimBLEDevice::startAdvertising();
        }
    }
};

static void wifiCfgUpdateValue() {
    if (!g_wifiCfgChar) return;
    char buf[48];
    g_wifiManager.fillStats(buf, sizeof(buf));
    for (char* p = buf; *p; p++) if (*p == '\n') *p = ',';
    g_wifiCfgChar->setValue(buf);
}

class WifiCfgCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*info*/) override {
        std::string v = c->getValue();
        if (v.empty()) return;
        // Protocol: byte[0]=mode(0-3), byte[1..32]=SSID, byte[33..96]=password
        WifiMgrMode mode = static_cast<WifiMgrMode>(v[0] & 0x03);
        char ssid[33] = {};
        char pass[64] = {};
        if (v.size() > 1)  strncpy(ssid, v.c_str() + 1,  32);
        if (v.size() > 33) strncpy(pass, v.c_str() + 33, 63);
        g_wifiManager.configure(mode, ssid, pass);
        wifiCfgUpdateValue();
    }
};

CmdCallbacks     g_cmdCb;
WifiCfgCallbacks g_wifiCfgCb;
ServerCallbacks  g_serverCb;

}  // namespace

bool begin(const char* deviceName) {
    if (g_initialised) return true;

    NimBLEDevice::init(deviceName ? deviceName : "ChipperZero");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(128);
    NimBLEDevice::setSecurityAuth(false, false, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(&g_serverCb);

    NimBLEService* svc = g_server->createService(kSvcUuid);

    NimBLECharacteristic* cmdChar = svc->createCharacteristic(
        kCmdUuid,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    cmdChar->setCallbacks(&g_cmdCb);

    g_statusChar = svc->createCharacteristic(
        kStatusUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    g_statusChar->setValue("ChipperZero v0.1");

    g_fbChar = svc->createCharacteristic(
        kFbUuid,
        NIMBLE_PROPERTY::NOTIFY);

    g_logChar = svc->createCharacteristic(
        kLogUuid,
        NIMBLE_PROPERTY::NOTIFY);

    g_wifiCfgChar = svc->createCharacteristic(
        kWifiCfgUuid,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ);
    g_wifiCfgChar->setCallbacks(&g_wifiCfgCb);
    wifiCfgUpdateValue();

    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(kSvcUuid);
    adv->setMinInterval(160);  // 100ms
    adv->setMaxInterval(160);
    adv->enableScanResponse(true);

    g_initialised = true;
    return true;
}

void start() {
    if (!g_initialised) return;
    if (g_enabled) return;
    NimBLEDevice::startAdvertising();
    g_enabled = true;
}

void stop() {
    if (!g_initialised) return;
    if (!g_enabled) return;
    NimBLEDevice::stopAdvertising();
    if (g_server) {
        // Politely disconnect any client.
        std::vector<uint16_t> peers = g_server->getPeerDevices();
        for (uint16_t handle : peers) {
            g_server->disconnect(handle);
        }
    }
    g_enabled = false;
}

void setEnabled(bool on) { on ? start() : stop(); }

void reinit(const char* deviceName) {
    g_initialised  = false;
    g_enabled      = false;
    g_connected    = false;
    g_server       = nullptr;
    g_statusChar   = nullptr;
    g_fbChar       = nullptr;
    g_logChar      = nullptr;
    g_wifiCfgChar  = nullptr;
    begin(deviceName);
    start();
}

bool isEnabled()   { return g_enabled; }
bool isConnected() { return g_connected; }

bool takeNewConnectionFlag() {
    if (!g_newConnection) return false;
    g_newConnection = false;
    return true;
}

void sendLog(const char* msg, size_t len) {
    if (!g_initialised || !g_connected || !g_logChar || !msg || len == 0) return;
    // BLE ATT payload cap: MTU(247) - 3 = 244 bytes. Truncate if needed.
    constexpr size_t kMax = 244;
    if (len > kMax) len = kMax;
    g_logChar->setValue(reinterpret_cast<const uint8_t*>(msg), len);
    g_logChar->notify();
    // Small gap so the BLE TX queue can drain before the next notification.
    vTaskDelay(pdMS_TO_TICKS(10));
}

void sendFrame(const uint8_t* buf, size_t len) {    if (!g_initialised || !g_connected || !g_fbChar || !buf || len == 0) return;

    constexpr size_t kChunkPayload = 124;  // fits in MTU 128 minus ATT header (3) minus chunk-idx (1)
    uint8_t pkt[kChunkPayload + 1];
    uint8_t idx = 0;
    for (size_t off = 0; off < len; off += kChunkPayload, ++idx) {
        size_t n = (len - off < kChunkPayload) ? (len - off) : kChunkPayload;
        pkt[0] = idx;
        memcpy(pkt + 1, buf + off, n);
        g_fbChar->setValue(pkt, n + 1);
        g_fbChar->notify();
        // Small gap so the BLE stack can drain its tx queue between chunks.
        delay(2);
    }
}

}  // namespace ble_remote
