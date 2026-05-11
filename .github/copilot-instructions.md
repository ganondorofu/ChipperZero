# Copilot instructions for ChipperZero

## Repository status

This repo is currently **docs-only**. `src/` and `assets/` exist but are empty — there is no `ChipperZero.ino`, no module implementations, and no build/test/lint/CI/package-manager configuration yet. `docs/architecture.md` and `docs/wiring.md` are the source of truth and define the contract that future code must follow.

When asked to add code, scaffold against the layout below; do not invent a different structure.

## Build, test, and lint commands

None configured yet (no Arduino CLI, PlatformIO, or CI). Target platform per docs: ESP32 with Arduino core.

## High-level architecture (from `docs/architecture.md`)

Intended runtime model:

- `ChipperZero.ino` owns Arduino `setup()`/`loop()`: initialize HAL, initialize UI, register modules, then run `encoder.tick()` → `menu.update()` → `display.flush()`.
- Arduino default Core 1 runs the main loop. Active module work runs as FreeRTOS tasks on **Core 0** (NimBLE, NRF, and WiFi all require Core 0).
- `src/hal/` — hardware wrappers and pin definitions: `pins.h` (GPIO constants), `power` (battery ADC + TP4056 STAT), `display` (U8g2 SH1106 wrapper + dirty flag), `encoder` (rotary + buttons polling + debounce).
- `src/ui/` — `menu` (menu system + `activeModule` exclusion) and `screen` (rendering + status bar).
- `src/modules/` — feature modules behind `IModule` (`module_base.h`): `init()`, `isAvailable()`, `start()`, `stop()`, `name()`. Planned: `nrf_spam`, `ble_spam`, `nfc`, `ir`, `wifi_scan`, `storage`.

### Concurrency and bus sharing

- **Single-module exclusion**: `menu.cpp` holds `IModule* activeModule`. `launchModule()` returns immediately if non-null; `stopActiveModule()` clears it. Don't bypass this guard.
- **VSPI mutex**: NRF24L01 (CSN GPIO27, CE GPIO4) and SD (CS GPIO15) share VSPI (SCK 18 / MISO 19 / MOSI 23). A global `SemaphoreHandle_t spi_mutex` (created in `setup()`) must be taken with `xSemaphoreTake(spi_mutex, portMAX_DELAY)` around every SPI access and released with `xSemaphoreGive`.
- **Shared I2C**: OLED SH1106 (0x3C) and PN532 (0x48) both sit on GPIO21 (SDA) / GPIO22 (SCL).

### Display

- Library: `U8g2` — `U8G2_SH1106_128X64_NONAME_F_HW_I2C`.
- Dirty-flag pattern: call `markDirty()` after any UI change; `flush()` skips the ~20 ms I2C transfer when not dirty.

### Input

- `InputEvent { NONE, UP, DOWN, OK, BACK, EXTRA }` returned by `encoder.tick()` (call every `loop()`).
- Rotation: grey-code on A/B; buttons: 5 ms `millis()` debounce.

### Status bar

Left = `"ChipperZero"` or `activeModule->name()` while running. Right = `power.getBatteryPercent()`, replaced by `"CHG"` when `power.isCharging()` is true.

## Key conventions

- Layering: hardware/platform details in HAL, feature behavior in modules, menu/display behavior in UI. Don't reach across layers (e.g., modules should not poke the OLED directly — go through `display`).
- New modules implement `IModule`; return `false` from `init()`/`isAvailable()` when hardware or deps are missing so the menu greys them out instead of crashing.
- Run all module FreeRTOS tasks on **Core 0**.
- Always guard SD or NRF SPI access with `spi_mutex`. Never access them simultaneously without the mutex, even briefly.
- Call `markDirty()` whenever UI state changes; never call the U8g2 send routine directly in `loop()`.
- **GPIO34 (OK), GPIO35 (BACK), GPIO36 (EXTRA) are INPUT_ONLY** — `INPUT_PULLUP` has no effect; external 10 kΩ pullups to 3.3 V are required. Do not assign output functions to these pins.
- Preserve pin assignments in `docs/wiring.md`. In particular: NRF **CSN is GPIO27** (moved off GPIO22 to avoid the I2C conflict); TP4056 STAT on GPIO13 is **active-LOW** (LOW = charging) and needs a 10 kΩ pullup.
- NRF24L01+PA+LNA can pull ~115 mA on TX — assume a 100 µF cap on its VCC; don't add code paths that TX during other heavy loads without considering brown-out.

## Libraries (planned, per `docs/architecture.md`)

U8g2 (OLED), RF24 (NRF24L01), NimBLE-Arduino (ESP32 BLE), Adafruit PN532 (NFC, stub), SD (built-in, stub), IRremote (IR TX, stub). Treat PN532, SD, and IR modules as stubs until called for.
