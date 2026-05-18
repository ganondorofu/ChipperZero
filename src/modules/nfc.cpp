#include "nfc.h"

#include <Arduino.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../hal/pins.h"

NfcModule g_nfc;

extern SemaphoreHandle_t spi_mutex;

static Adafruit_PN532 s_pn532(-1, -1);  // I2C mode

// Last card in memory (shared between modes)
static NfcCard s_card;
static bool    s_cardValid = false;

// ---- PN532 raw emulation constants ------------------------------------------
#define PN532_CMD_TGINITASTARGET  0x8C
#define PN532_CMD_TGGETDATA       0x86
#define PN532_CMD_TGSETDATA       0x8E
#define PN532_MIFARE_READ         0x30
#define PN532_MIFARE_WRITE        0xA2

// ---- helpers ----------------------------------------------------------------

void NfcModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

static void uidToStr(const uint8_t* uid, uint8_t len, char* out, size_t outLen) {
    size_t pos = 0;
    for (uint8_t i = 0; i < len && pos + 3 < outLen; i++) {
        if (i > 0) out[pos++] = ':';
        snprintf(out + pos, outLen - pos, "%02X", uid[i]);
        pos += 2;
    }
}

// ---- SD helpers -------------------------------------------------------------

static bool sdSaveCard(const NfcCard& card, uint8_t idx) {
    if (!spi_mutex) return false;
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    if (!SD.exists("/nfc")) SD.mkdir("/nfc");
    char path[24];
    snprintf(path, sizeof(path), "/nfc/%03u.nfc", idx);
    File f = SD.open(path, FILE_WRITE);
    bool ok = false;
    if (f) {
        f.printf("tag:%s\ntype:%u\nuidlen:%u\n",
                 card.tag, (uint8_t)card.isClassic, card.uidLen);
        f.print("uid:");
        for (uint8_t i = 0; i < card.uidLen; i++) {
            f.printf("%02X", card.uid[i]);
            if (i + 1 < card.uidLen) f.print(':');
        }
        f.print('\n');
        f.printf("datalen:%u\ndata:", card.dataLen);
        for (uint16_t i = 0; i < card.dataLen; i++)
            f.printf("%02X", card.data[i]);
        f.print('\n');
        f.close();
        ok = true;
    }
    xSemaphoreGive(spi_mutex);
    return ok;
}

static bool sdLoadCard(uint8_t idx, NfcCard& card) {
    if (!spi_mutex) return false;
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    char path[24];
    snprintf(path, sizeof(path), "/nfc/%03u.nfc", idx);
    File f = SD.open(path);
    if (!f) { xSemaphoreGive(spi_mutex); return false; }

    memset(&card, 0, sizeof(card));
    char line[2048];
    while (f.available()) {
        int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        if (strncmp(line, "tag:", 4) == 0)
            strncpy(card.tag, line + 4, sizeof(card.tag) - 1);
        else if (strncmp(line, "type:", 5) == 0)
            card.isClassic = (atoi(line + 5) != 0);
        else if (strncmp(line, "uidlen:", 7) == 0)
            card.uidLen = atoi(line + 7);
        else if (strncmp(line, "uid:", 4) == 0) {
            const char* p = line + 4;
            for (uint8_t i = 0; i < card.uidLen && *p; i++) {
                card.uid[i] = (uint8_t)strtol(p, nullptr, 16);
                p += 3; // "XX:"
            }
        } else if (strncmp(line, "datalen:", 8) == 0)
            card.dataLen = atoi(line + 8);
        else if (strncmp(line, "data:", 5) == 0) {
            const char* p = line + 5;
            char hex[3] = {0};
            for (uint16_t i = 0; i < card.dataLen && p[0] && p[1]; i++) {
                hex[0] = p[0]; hex[1] = p[1];
                card.data[i] = (uint8_t)strtol(hex, nullptr, 16);
                p += 2;
            }
        }
    }
    f.close();
    xSemaphoreGive(spi_mutex);
    return card.uidLen > 0;
}

// ---- Read task --------------------------------------------------------------

static void nfcReadTask(void* arg) {
    NfcModule* self = reinterpret_cast<NfcModule*>(arg);
    s_pn532.SAMConfig();
    self->setStatus("Waiting for tag...");

    // Find next save slot
    uint8_t saveIdx = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            char path[24];
            snprintf(path, sizeof(path), "/nfc/%03u.nfc", i);
            bool exists = SD.exists(path);
            xSemaphoreGive(spi_mutex);
            if (!exists) { saveIdx = i; break; }
            saveIdx = (i + 1) % 8;
        }
    }

    while (self->isRunning()) {
        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        if (!s_pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 500)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!self->isRunning()) break;

        NfcCard& card = s_card;
        memset(&card, 0, sizeof(card));
        memcpy(card.uid, uid, uidLen);
        card.uidLen = uidLen;
        card.isClassic = (uidLen == 4);
        snprintf(card.tag, sizeof(card.tag), "nfc_%03u", saveIdx);

        char uidStr[22];
        uidToStr(uid, uidLen, uidStr, sizeof(uidStr));

        if (card.isClassic) {
            // Try to read all sectors with default keys
            uint8_t keyA[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            uint16_t dIdx = 0;
            for (uint8_t blk = 0; blk < 64 && dIdx + 16 <= 1024; blk++) {
                uint8_t sector = blk / 4;
                if (blk % 4 == 0) {
                    s_pn532.mifareclassic_AuthenticateBlock(uid, uidLen, blk, 0, keyA);
                }
                uint8_t buf[16];
                if (s_pn532.mifareclassic_ReadDataBlock(blk, buf)) {
                    memcpy(&card.data[dIdx], buf, 16);
                } else {
                    memset(&card.data[dIdx], 0, 16);
                }
                dIdx += 16;
                (void)sector;
            }
            card.dataLen = dIdx;
        } else {
            // Ultralight: read pages 0-15
            for (uint8_t pg = 0; pg < 16; pg++) {
                uint8_t buf[4];
                if (s_pn532.mifareultralight_ReadPage(pg, buf)) {
                    memcpy(&card.data[pg * 4], buf, 4);
                }
            }
            card.dataLen = 64;
        }

        s_cardValid = true;
        bool saved = sdSaveCard(card, saveIdx);
        saveIdx = (saveIdx + 1) % 8;

        char buf[48];
        const char* type = card.isClassic ? "Classic" : "Ultralight";
        snprintf(buf, sizeof(buf), "%s %uB\n%s%s",
                 type, card.dataLen, uidStr,
                 saved ? " [saved]" : "");
        self->setStatus(buf);
        vTaskDelay(pdMS_TO_TICKS(2000));
        self->setStatus("Waiting for tag...");
    }

    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- Write (clone) task -----------------------------------------------------

static void nfcWriteTask(void* arg) {
    NfcModule* self = reinterpret_cast<NfcModule*>(arg);

    // Load last card (try SD slot 0 first if no in-memory card)
    if (!s_cardValid) {
        if (!sdLoadCard(0, s_card)) {
            self->setStatus("No card in memory\nRead a card first");
            while (self->isRunning()) vTaskDelay(pdMS_TO_TICKS(200));
            self->clearTask();
            vTaskDelete(nullptr);
            return;
        }
        s_cardValid = true;
    }

    char uidStr[22];
    uidToStr(s_card.uid, s_card.uidLen, uidStr, sizeof(uidStr));
    char buf[48];
    snprintf(buf, sizeof(buf), "Src: %s\nPlace blank card", uidStr);
    self->setStatus(buf);

    while (self->isRunning()) {
        uint8_t uid[7] = {0};
        uint8_t uidLen = 0;
        if (!s_pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 500)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!self->isRunning()) break;

        bool ok = false;
        if (uidLen == 7 && !s_card.isClassic) {
            // Ultralight: write pages 3-15
            uint8_t cc[4] = {0xE1, 0x10, 0x06, 0x00};
            s_pn532.mifareultralight_WritePage(3, cc);
            for (uint8_t pg = 4; pg < 16; pg++) {
                s_pn532.mifareultralight_WritePage(pg, &s_card.data[pg * 4]);
            }
            ok = true;
        } else if (uidLen == 4 && s_card.isClassic) {
            uint8_t keyA[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            ok = true;
            for (uint8_t blk = 1; blk < 64; blk++) {  // skip block 0 (manufacturer)
                if (blk % 4 == 3) continue;            // skip sector trailers
                if (blk % 4 == 0) {
                    if (!s_pn532.mifareclassic_AuthenticateBlock(uid, uidLen, blk, 0, keyA))
                    { ok = false; break; }
                }
                if (!s_pn532.mifareclassic_WriteDataBlock(blk, &s_card.data[blk * 16]))
                { ok = false; break; }
            }
        }

        self->setStatus(ok ? "Clone OK!\nBack to exit" : "Write failed\nWrong card type?");
        vTaskDelay(pdMS_TO_TICKS(3000));
        snprintf(buf, sizeof(buf), "Src: %s\nPlace blank card", uidStr);
        self->setStatus(buf);
    }

    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- Emulate task -----------------------------------------------------------

static void nfcEmulateTask(void* arg) {
    NfcModule* self = reinterpret_cast<NfcModule*>(arg);

    if (!s_cardValid) {
        if (!sdLoadCard(0, s_card)) {
            self->setStatus("No card in memory\nRead a card first");
            while (self->isRunning()) vTaskDelay(pdMS_TO_TICKS(200));
            self->clearTask();
            vTaskDelete(nullptr);
            return;
        }
        s_cardValid = true;
    }

    char uidStr[22];
    uidToStr(s_card.uid, s_card.uidLen, uidStr, sizeof(uidStr));
    const char* type = s_card.isClassic ? "Classic" : "Ultralight";

    char buf[48];
    snprintf(buf, sizeof(buf), "Emulating %s\n%s", type, uidStr);
    self->setStatus(buf);

    // Build TgInitAsTarget command
    // SENS_RES (ATQA): Classic = {0x04,0x00}, Ultralight = {0x44,0x00}
    uint8_t atqaH = s_card.isClassic ? 0x04 : 0x44;
    uint8_t sak   = s_card.isClassic ? 0x20 : 0x00;

    uint8_t cmd[16];
    uint8_t n = 0;
    cmd[n++] = PN532_CMD_TGINITASTARGET;
    cmd[n++] = 0x00;    // mode: passive, 14443-3A
    cmd[n++] = atqaH;   // ATQA byte 1
    cmd[n++] = 0x00;    // ATQA byte 2
    // NFCID1: first 3 bytes of UID (4-byte UID) or first 6 bytes (7-byte UID)
    uint8_t nfcidLen = (s_card.uidLen == 7) ? 6 : 3;
    for (uint8_t i = 0; i < nfcidLen; i++) cmd[n++] = s_card.uid[i];
    cmd[n++] = sak;
    cmd[n++] = 0;  // Gt length = 0
    cmd[n++] = 0;  // Tk length = 0

    while (self->isRunning()) {
        if (!s_pn532.sendCommandCheckAck(cmd, n, 100)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Wait for reader to initiate (PN532 handles REQA/anticol/SELECT)
        // After init, handle READ commands for Ultralight
        vTaskDelay(pdMS_TO_TICKS(200));

        if (!s_card.isClassic) {
            // Handle Ultralight READ PAGE commands from reader
            uint8_t resp[32] = {};
            uint8_t respLen = 0;
            uint8_t getCmd[2] = {PN532_CMD_TGGETDATA, 0x01};
            if (s_pn532.sendCommandCheckAck(getCmd, 2, 500)) {
                // PN532 received data from reader — parse and respond
                // For simplicity: respond with page data for READ (0x30) commands
                if (resp[0] == PN532_MIFARE_READ && respLen > 1) {
                    uint8_t page = resp[1] & 0x0F;
                    uint8_t setData[18];
                    setData[0] = PN532_CMD_TGSETDATA;
                    setData[1] = 0x01;
                    memcpy(&setData[2], &s_card.data[page * 4], 16);
                    s_pn532.sendCommandCheckAck(setData, 18, 200);
                }
            }
        }
        // For Classic: PN532 handles ATQA/SAK; auth will fail (no Crypto1)
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- Suica balance task -----------------------------------------------------

static const char* felicaCardName(uint16_t sysCode) {
    switch (sysCode) {
        case 0x88B4: return "Suica";
        case 0x88B6: return "PASMO";
        case 0x88D1: return "ICOCA";
        case 0x88A0: return "Kitaca";
        case 0x8A0C: return "Hayakaken";
        case 0x8145: return "nimoca";
        case 0x8B5D: return "manaca";
        case 0x88B7: return "WAON";
        case 0x88C1: return "nanaco";
        case 0xFE00: return "Edy";
        case 0x0003: return "IC Card";
        default:     return "FeliCa";
    }
}

static void nfcSuicaTask(void* arg) {
    NfcModule* self = reinterpret_cast<NfcModule*>(arg);
    s_pn532.SAMConfig();
    self->setStatus("IC/FeliCa: tap...");

    while (self->isRunning()) {
        uint8_t idm[8] = {};
        uint8_t pmm[8] = {};
        uint16_t sysCode = 0;

        // Poll for any FeliCa card (0xFFFF = any)
        if (!s_pn532.felica_Poll(0xFF, 0xFF, idm, pmm, &sysCode)) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        if (!self->isRunning()) break;

        const char* cardName = felicaCardName(sysCode);

        // Debug output
        Serial.printf("[NFC] SysCode: %04X  Card: %s\n", sysCode, cardName);
        Serial.printf("[NFC] IDm: %02X%02X%02X%02X%02X%02X%02X%02X\n",
                      idm[0], idm[1], idm[2], idm[3],
                      idm[4], idm[5], idm[6], idm[7]);

        // Try to read SF balance (service 0x008B, block 0)
        uint8_t block[16] = {};
        bool ok = s_pn532.felica_ReadWithoutEncryption(idm, 0x008B, 0, block);

        if (ok) {
            Serial.print("[NFC] Block0(0x008B): ");
            for (int i = 0; i < 16; i++) Serial.printf("%02X ", block[i]);
            Serial.println();
        }

        char buf[48];
        if (ok) {
            // Balance at bytes 10-11, little-endian (JPY)
            uint16_t balance = (uint16_t)block[10] | ((uint16_t)block[11] << 8);
            snprintf(buf, sizeof(buf), "%s\n\xA5%u", cardName, (unsigned)balance);
        } else {
            snprintf(buf, sizeof(buf), "%s\nNo balance svc", cardName);
        }
        self->setStatus(buf);

        // Wait before next poll
        vTaskDelay(pdMS_TO_TICKS(2500));
        self->setStatus("IC/FeliCa: tap...");
    }

    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- IModule ----------------------------------------------------------------

bool NfcModule::init() {
    s_pn532.begin();
    // s_pn532.begin() calls Wire.begin() with default pins (SCL=22).
    // Wire.end() + Wire.begin() forces full reconfiguration to correct pins.
    Wire.end();
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setTimeOut(200);
    delay(20);
    uint32_t ver = s_pn532.getFirmwareVersion();
    if (!ver) { available_ = false; return false; }
    s_pn532.SAMConfig();
    available_ = true;
    return true;
}

void NfcModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    TaskFunction_t fn = nullptr;
    switch (mode_) {
        case NfcMode::READ:    fn = nfcReadTask;    break;
        case NfcMode::WRITE:   fn = nfcWriteTask;   break;
        case NfcMode::EMULATE: fn = nfcEmulateTask; break;
        case NfcMode::SUICA:   fn = nfcSuicaTask;   break;
    }
    xTaskCreatePinnedToCore(fn, "nfc", 8192, this, 1, &task_, 0);
}

void NfcModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 60 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void NfcModule::onEvent(uint8_t ev) { (void)ev; }

void NfcModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
