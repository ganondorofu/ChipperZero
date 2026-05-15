#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "hal/ble_remote.h"
#include "hal/nrf_radio.h"
#include "hal/display.h"
#include "hal/encoder.h"
#include "hal/power.h"
#include "modules/ble_spam.h"
#include "modules/ir.h"
#include "modules/nfc.h"
#include "modules/nrf_spam.h"
#include "modules/nrf_ble_spam.h"
#include "modules/nrf_jammer.h"
#include "modules/storage.h"
#include "modules/wifi_scan.h"
#include "modules/wifi_beacon_clone.h"
#include "modules/wifi_evil_portal.h"
#include "modules/wifi_spectrum.h"
#include "modules/nrf_mousejack.h"
#include "ui/menu.h"

SemaphoreHandle_t spi_mutex = nullptr;

// ---- Boot screen ------------------------------------------------------------
//
//  y= 0-12  [CHIPPERZERO v0.1]   inverted header
//  y=13     ────────────────────
//  y=14-37  ASCII art  (4 lines × 6px, 4x6 font)
//  y=38     ────────────────────
//  y=40-47  > status msg        (5x7 font)
//  y=49-55  [████░░░░] n/total  progress bar
//

static const char* kArt[] = {
    " ___ _    _                ",
    "/ __| |_ (_)_ __ _ __  __ ",
    "| (__| ' \\| | '_ \\ '_ \\/ -_)",
    " \\___|_||_|_| .__/_|  \\___|",
    "  ____      |_|             ",
    " |_  /__ _ _ __ ___        ",
    "  / // -_| '_/ _ \\         ",
    " /___\\___|_| \\___/         ",
};

static uint8_t g_bootStep  = 0;
static uint8_t g_bootTotal = 14;

static void bootScreen(const char* msg) {
    auto& g = display::u8g2();
    g.clearBuffer();

    // Inverted header
    g.drawBox(0, 0, 128, 13);
    g.setDrawColor(0);
    g.setFont(u8g2_font_7x13B_tf);
    g.drawStr(2, 11, "CHIPPERZERO");
    g.setFont(u8g2_font_4x6_tf);
    g.drawStr(102, 11, "v0.1");
    g.setDrawColor(1);

    // ASCII art glitch/reveal
    static const char kGlitchChars[] = "#|/\\+-=~!?@";
    g.setFont(u8g2_font_4x6_tf);
    uint8_t linesShown = g_bootStep < 4 ? g_bootStep : 4;
    uint8_t glitchN = 0;
    if (g_bootStep >= 4) {
        int32_t rem = (int32_t)g_bootTotal - (int32_t)g_bootStep;
        glitchN = (uint8_t)(rem > 6 ? 3 : rem > 0 ? rem / 2 : 0);
    }
    for (uint8_t i = 0; i < linesShown; i++) {
        char line[33];
        strncpy(line, kArt[i], 32); line[32] = '\0';
        uint8_t applied = 0, len = strlen(line);
        for (uint8_t k = 0; k < len && applied < glitchN; k++) {
            uint8_t pos = (uint8_t)((g_bootStep * 19u + i * 13u + k * 7u) % len);
            if (line[pos] != ' ') {
                line[pos] = kGlitchChars[(g_bootStep + i + k) % (sizeof(kGlitchChars) - 1)];
                applied++;
            }
        }
        g.drawStr(0, 19 + i * 6, line);
    }
    if (g_bootStep < 4 && linesShown > 0)
        g.drawHLine(0, 14 + (linesShown - 1) * 6, 128);

    g.drawHLine(0, 39, 128);

    // Status message
    g.setFont(u8g2_font_5x7_tf);
    char buf[26];
    snprintf(buf, sizeof(buf), "> %s", msg);
    g.drawStr(0, 48, buf);

    // Progress bar
    constexpr uint8_t kBX = 0, kBY = 51, kBW = 104, kBH = 6;
    g.drawFrame(kBX, kBY, kBW, kBH);
    uint8_t fill = (uint8_t)((uint32_t)g_bootStep * (kBW - 2) / g_bootTotal);
    if (fill > 0) g.drawBox(kBX + 1, kBY + 1, fill, kBH - 2);

    // Step counter
    g.setFont(u8g2_font_4x6_tf);
    char sc[8];
    snprintf(sc, sizeof(sc), "%u/%u", g_bootStep, g_bootTotal);
    g.drawStr(107, 57, sc);

    g.sendBuffer();
    g_bootStep++;
}

// ---- Splash animation (Matrix rain) -----------------------------------------
// 4x6 font → 32 cols × 10 rows. Each column has a falling "head" + 4-char trail.
// Runs for ~1.5s then shows logo before handing off to menu.

static void splashAnimation() {
    auto& g = display::u8g2();

    constexpr uint8_t kCols   = 32;
    constexpr uint8_t kRows   = 11;   // 64px / 6px = ~10.6 rows
    constexpr uint8_t kFrames = 9;
    constexpr uint8_t kTrail  = 5;
    constexpr uint8_t kFontW  = 4;
    constexpr uint8_t kFontH  = 6;

    static const char kChars[] =
        "0123456789ABCDEF!@#$%+-=/?|\\<>";
    constexpr uint8_t kCharCount = sizeof(kChars) - 1;

    int8_t  head[kCols];
    uint8_t spd [kCols];
    for (uint8_t c = 0; c < kCols; c++) {
        head[c] = -(int8_t)(esp_random() % kRows);
        spd[c]  = 1 + (esp_random() % 3);
    }

    for (uint8_t frame = 0; frame < kFrames; frame++) {
        g.clearBuffer();
        g.setFont(u8g2_font_4x6_tf);

        for (uint8_t c = 0; c < kCols; c++) {
            for (uint8_t t = 0; t <= kTrail; t++) {
                int8_t row = head[c] - (int8_t)t;
                if (row < 0 || row >= kRows) continue;
                char ch;
                if (t == 0) {
                    // Head: dense symbol
                    ch = kChars[esp_random() % kCharCount];
                } else if (t <= 2) {
                    ch = kChars[esp_random() % kCharCount];
                } else {
                    // Tail fades to sparse dots
                    ch = (esp_random() % 3 == 0) ? '.' : ' ';
                }
                if (ch != ' ') {
                    g.drawGlyph(c * kFontW, (row + 1) * kFontH, ch);
                }
            }
            if (frame % spd[c] == 0) head[c]++;
        }

        // Last 3 frames: overlay "CHIPPERZERO" in large font
        if (frame >= kFrames - 5) {
            // u8g2_font_10x20_tf: 10px wide → 11 chars = 110px, 20px tall
            g.drawBox(4, 20, 120, 26);
            g.setDrawColor(0);
            g.setFont(u8g2_font_10x20_tf);
            g.drawStr(9, 41, "CHIPPERZERO");
            g.setDrawColor(1);
            g.setFont(u8g2_font_4x6_tf);
        }

        g.sendBuffer();
        delay(55);
    }
}


void setup() {
    Serial.begin(115200);
    spi_mutex = xSemaphoreCreateMutex();

    bool dispOk = display::begin();
    Serial.println(dispOk ? "Display OK" : "Display FAILED");
    bootScreen("Display OK");

    power::begin();
    bootScreen("Power OK");

    encoder::begin();
    bootScreen("Encoder OK");

    bootScreen("NRF24 init...");
    g_nrfSpam.init();
    g_nrfBleSpam.init();
    g_nrfJammer.init();
    g_nrfMousejack.init();
    bootScreen("NRF24 OK");

    bootScreen("BLE Spam...");
    g_bleSpam.init();
    bootScreen("NFC...");
    g_nfc.init();
    display::reinit();  // NFC resets Wire; restore I2C pins and OLED
    bootScreen("IR...");
    g_ir.init();
    bootScreen("WiFi...");
    g_wifiScan.init();
    g_wifiBeaconClone.init();
    g_wifiEvilPortal.init();
    g_wifiSpectrum.init();
    bootScreen("Storage...");
    g_storage.init();
    bootScreen("BLE Remote...");
    if (ble_remote::begin("ChipperZero")) {
        ble_remote::start();
        Serial.println("BLE Remote OK");
    } else {
        Serial.println("BLE Remote FAILED");
    }
    bootScreen("Ready!");

    splashAnimation();

    menu::begin();
}

void loop() {
    menu::update();
    display::flush();
}
