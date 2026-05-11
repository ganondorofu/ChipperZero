# ChipperZero Wiring Guide

## Pin Assignment

| Function | GPIO | Note |
|----------|------|------|
| NRF CE | 4 | |
| NRF CSN | 27 | moved from 22 (I2C conflict) |
| SPI SCK | 18 | VSPI |
| SPI MISO | 19 | VSPI |
| SPI MOSI | 23 | VSPI |
| I2C SDA | 21 | OLED + PN532 shared |
| I2C SCL | 22 | OLED + PN532 shared |
| Rotary A | 32 | internal pullup OK |
| Rotary B | 33 | internal pullup OK |
| Rotary SW (OK) | 14 | INPUT_PULLUP 対応、外部抵抗不要 |
| Button BACK | 16 | INPUT_PULLUP 対応、外部抵抗不要 |
| Button EXTRA | 17 | INPUT_PULLUP 対応、外部抵抗不要 |
| TP4056 STAT | 13 | LOW = charging |
| SD CS | 15 | VSPI |
| IR TX | 26 | |
| IR RX | 25 | optional |

---

## GPIO34/35/36 — 注意（未使用）

GPIO34, 35, 36 は INPUT_ONLY で内部プルアップなし。ボタンは GPIO14/16/17 に移動済みのため、これらのピンは使用しない。

---

## SPI Bus (VSPI) — NRF24L01 + SD Card

Two devices share VSPI. Each has its own CS line.

```
ESP32 GPIO18 (SCK)  ─────┬─── NRF24L01 SCK
ESP32 GPIO19 (MISO) ─────┼─── NRF24L01 MISO
ESP32 GPIO23 (MOSI) ─────┼─── NRF24L01 MOSI
ESP32 GPIO27 (CSN)  ─────┘    NRF24L01 CSN
ESP32 GPIO4  (CE)   ───────── NRF24L01 CE

ESP32 GPIO18 (SCK)  ─────┬─── SD SCK
ESP32 GPIO19 (MISO) ─────┼─── SD MISO
ESP32 GPIO23 (MOSI) ─────┼─── SD MOSI
ESP32 GPIO15 (CS)   ─────┘    SD CS
```

Access is protected by a FreeRTOS mutex. Do not access SD and NRF simultaneously.

---

## I2C Bus — OLED SH1106 + PN532

Two devices share I2C (GPIO21/22). Addresses do not conflict.

| Device | I2C Address |
|--------|-------------|
| OLED SH1106 | 0x3C |
| PN532 (ELECHOUSE V3) | 0x48 |

```
ESP32 GPIO21 (SDA) ──── OLED SDA
                   ──── PN532 SDA
ESP32 GPIO22 (SCL) ──── OLED SCL
                   ──── PN532 SCL
```

### PN532 I2C Mode Setup (ELECHOUSE V3)

Set the jumper/switch on the module:
- SW1 = OFF
- SW2 = ON

This selects I2C mode. Default from factory may vary — verify before powering on.

---

## TP4056 STAT Pin

```
TP4056 STAT ──── 10kΩ ──── 3.3V
            ──── ESP32 GPIO13
```

- STAT LOW = charging
- STAT HIGH (floating) = full / not charging
- The 10kΩ pullup prevents floating reads

---

## Power Rails

```
LiPo 3.7V ─── TP4056 (BAT+/BAT-) ─── SX1308 (5V boost) ─── USB devices
                                   ─── MP1584EN (3.3V buck) ─── ESP32 / NRF24 / OLED / PN532 / SD
```

NRF24L01+PA+LNA can draw up to 115mA on TX. Add 100µF capacitor on NRF VCC line to prevent brown-out.
