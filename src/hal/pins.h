#pragma once

// Pin map per docs/wiring.md. Do not change without updating that doc.

// --- VSPI (NRF24L01 + SD share this bus; protect with spi_mutex) ---
constexpr int PIN_SPI_SCK   = 18;
constexpr int PIN_SPI_MISO  = 19;
constexpr int PIN_SPI_MOSI  = 23;

// --- NRF24L01 ---
constexpr int PIN_NRF_CE    = 4;
constexpr int PIN_NRF_CSN   = 22;

// --- SD card ---
constexpr int PIN_SD_CS     = 15;

// --- I2C (OLED SH1106 0x3C + PN532 0x48) ---
constexpr int PIN_I2C_SDA   = 21;
constexpr int PIN_I2C_SCL   = 17;  // GPIO17 (repurposed from spare button)

// --- Rotary encoder (internal pullups OK on 32/33) ---
constexpr int PIN_ENC_A     = 33;
constexpr int PIN_ENC_B     = 32;

// --- Buttons. INPUT_PULLUP; no external resistors needed. ---
constexpr int PIN_BTN_OK    = 14;
constexpr int PIN_BTN_BACK  = 16;

// --- Power ---
constexpr int PIN_TP4056_STAT = 13;  // active-LOW (LOW = charging); needs 10k pullup
constexpr int PIN_BAT_ADC     = 39;  // VBAT divider (TBD: confirm divider ratio)

// --- IR ---
constexpr int PIN_IR_TX     = 27;
constexpr int PIN_IR_RX     = 25;
