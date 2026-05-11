# ChipperZero Architecture

## Directory Structure

```
ChipperZero/
├── ChipperZero.ino          # main sketch
├── src/
│   ├── hal/
│   │   ├── pins.h           # all GPIO constants + INPUT_ONLY warnings
│   │   ├── power.h/.cpp     # battery ADC stub + TP4056 STAT
│   │   ├── display.h/.cpp   # U8g2 SH1106 wrapper + dirty flag
│   │   └── encoder.h/.cpp   # rotary + buttons polling + debounce
│   ├── ui/
│   │   ├── menu.h/.cpp      # menu system + activeModule exclusion
│   │   └── screen.h/.cpp    # menu rendering + status bar
│   └── modules/
│       ├── module_base.h    # IModule pure virtual interface
│       ├── nrf_spam.h/.cpp  # NRF24L01 BLE spam (ported from NrfBleSpam.ino)
│       ├── ble_spam.h/.cpp  # ESP32 NimBLE spam (stub)
│       ├── nfc.h/.cpp       # PN532 (stub)
│       ├── ir.h/.cpp        # IR TX (stub)
│       ├── wifi_scan.h/.cpp # WiFi scanner (stub)
│       └── storage.h/.cpp   # SD card (stub)
├── assets/
└── docs/
    ├── wiring.md
    └── architecture.md
```

---

## Execution Model

```
Core 1 (Arduino default)
  setup() → init HAL → init UI → register modules → start menu
  loop()  → encoder.tick() → menu.update() → display.flush()

Core 0 (FreeRTOS)
  active module task — only one module may run at a time
```

---

## Module Interface (module_base.h)

```cpp
class IModule {
public:
    virtual bool        init()        = 0;  // false → greyed out in menu
    virtual bool        isAvailable() = 0;
    virtual void        start()       = 0;
    virtual void        stop()        = 0;
    virtual const char* name()        = 0;
};
```

---

## Module Exclusion Policy

Only one module may be active at a time. `menu.cpp` enforces this:

```cpp
IModule* activeModule = nullptr;

void launchModule(IModule* m) {
    if (activeModule != nullptr) return;  // block double-launch
    activeModule = m;
    m->start();
}
void stopActiveModule() {
    if (activeModule) { activeModule->stop(); activeModule = nullptr; }
}
```

All FreeRTOS tasks run on Core 0 (NimBLE, NRF, WiFi all require Core 0).

---

## SPI Bus Mutex

NRF and SD share VSPI. A global FreeRTOS mutex prevents bus contention:

```cpp
// ChipperZero.ino
SemaphoreHandle_t spi_mutex;

// setup()
spi_mutex = xSemaphoreCreateMutex();

// usage in each driver
xSemaphoreTake(spi_mutex, portMAX_DELAY);
// ... SPI access ...
xSemaphoreGive(spi_mutex);
```

---

## Display (display.h)

Library: `U8g2` — `U8G2_SH1106_128X64_NONAME_F_HW_I2C`

Dirty flag prevents unnecessary I2C transfers (~20ms each):

```cpp
void markDirty();  // call after any UI change
void flush();      // sends buffer only if dirty
```

---

## Encoder (encoder.h)

```cpp
enum InputEvent { NONE, UP, DOWN, OK, BACK, EXTRA };
InputEvent tick();  // call every loop()
```

- Rotation: grey-code comparison of previous/current A+B state
- Buttons (GPIO34/35/36): 5ms debounce via `millis()`
- `INPUT_PULLUP` is invalid on GPIO34/35/36 — external 10kΩ required

---

## Status Bar Layout

```
┌─────────────────────────────┐
│ ChipperZero    BAT:███░ 75% │
└─────────────────────────────┘
```

- Left: "ChipperZero" or activeModule->name() when running
- Right: battery percent from `power.getBatteryPercent()`
- "CHG" replaces percent while `power.isCharging()` is true

---

## Menu Structure

```
[ROOT]
 ├── BLE Spam
 │    ├── NRF Spam
 │    └── ESP BLE Spam
 ├── NFC
 ├── IR
 ├── WiFi
 └── System
      ├── Battery
      └── About
```

Greyed-out items: modules where `isAvailable()` returns false.

---

## Libraries

| Library | Purpose |
|---------|---------|
| U8g2 | SH1106 OLED |
| RF24 | NRF24L01 SPI |
| NimBLE-Arduino | ESP32 BLE |
| Adafruit PN532 | NFC (stub stage: not yet needed) |
| SD | SD card (built-in) |
| IRremote | IR TX (stub stage: not yet needed) |
