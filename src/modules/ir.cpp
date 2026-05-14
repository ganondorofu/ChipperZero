#include "ir.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../hal/encoder.h"
#include "../hal/pins.h"

IrModule g_ir;

extern SemaphoreHandle_t spi_mutex;

static IRsend g_irsend(PIN_IR_TX);
static IRrecv g_irrecv(PIN_IR_RX, 200, 15, true);

// NEC32: standard 8-bit NEC (addr + ~addr + cmd + ~cmd), MSB-first packing
static constexpr uint32_t NEC32(uint8_t a, uint8_t c) {
    return ((uint32_t)a << 24) | ((uint32_t)(~a & 0xFF) << 16) |
           ((uint32_t)c << 8)  | ((uint32_t)(~c & 0xFF));
}
// Samsung-style: addr repeated (not inverted)
static constexpr uint32_t NEC32S(uint8_t a, uint8_t c) {
    return ((uint32_t)a << 24) | ((uint32_t)a << 16) |
           ((uint32_t)c << 8)  | ((uint32_t)(~c & 0xFF));
}
// Panasonic payload: device, subdevice, command + checksum
static constexpr uint32_t PAN32(uint8_t dev, uint8_t sub, uint8_t cmd) {
    return ((uint32_t)dev << 24) | ((uint32_t)sub << 16) |
           ((uint32_t)cmd << 8)  | ((uint32_t)(dev ^ sub ^ cmd));
}

// ---- TV Kill codes ----------------------------------------------------------
struct TvCode { uint32_t code; const char* brand; };

static const TvCode kTvCodes[] = {
    { NEC32S(0x07, 0x02), "Samsung"   },
    { NEC32(0x04,  0x08), "LG"        },
    { NEC32(0x02,  0x48), "Toshiba"   },
    { NEC32(0x40,  0x12), "Toshiba B" },
    { NEC32(0x40,  0x08), "Sharp"     },
    { NEC32(0x00,  0x0C), "Philips"   },
    { NEC32(0x00,  0x08), "Hisense"   },
    { NEC32(0x01,  0x4D), "TCL"       },
    { NEC32(0x01,  0x00), "Hitachi"   },
    { NEC32(0x00,  0x28), "Haier"     },
};
static constexpr uint8_t kTvCount = sizeof(kTvCodes) / sizeof(kTvCodes[0]);

// ---- Preset database --------------------------------------------------------
// NEC entries: address=0 (unused), data=NEC32 code
// Panasonic entries: address=0x4004 (manufacturer), data=PAN32 payload
// Sony entries: address=bits (12/15/20), data=SIRC code value

static const IrPreset kPresets[] = {
    // ---- TV Power ----
    {0,"Samsung",    IrProto::NEC,      0,      NEC32S(0x07, 0x02)},
    {0,"Samsung B",  IrProto::NEC,      0,      NEC32S(0x04, 0x02)},
    {0,"LG",         IrProto::NEC,      0,      NEC32(0x04,  0x08)},
    {0,"LG B",       IrProto::NEC,      0,      NEC32(0x01,  0x08)},
    {0,"Sony",       IrProto::SONY,     12,     0x0A90},
    {0,"Toshiba",    IrProto::NEC,      0,      NEC32(0x02,  0x48)},  // confirmed
    {0,"Toshiba B",  IrProto::NEC,      0,      NEC32(0x40,  0x12)},  // confirmed
    {0,"Toshiba C",  IrProto::NEC,      0,      NEC32(0x02,  0x15)},
    {0,"Panasonic",  IrProto::PANASONIC,0x4004, PAN32(0x01,0x00,0xBC)},  // confirmed
    {0,"Panasonic B",IrProto::PANASONIC,0x4004, PAN32(0x02,0x00,0xBC)},
    {0,"Sharp",      IrProto::NEC,      0,      NEC32(0x40,  0x08)},
    {0,"Hisense",    IrProto::NEC,      0,      NEC32(0x00,  0x08)},
    {0,"TCL",        IrProto::NEC,      0,      NEC32(0x01,  0x4D)},
    {0,"Mitsubishi", IrProto::NEC,      0,      NEC32(0x05,  0x1E)},
    {0,"Hitachi",    IrProto::NEC,      0,      NEC32(0x01,  0x00)},
    // ---- TV Vol+ ----
    {1,"Samsung",    IrProto::NEC,      0,      NEC32S(0x07, 0x07)},
    {1,"LG",         IrProto::NEC,      0,      NEC32(0x04,  0x02)},
    {1,"Sony",       IrProto::SONY,     12,     0x0490},
    {1,"Toshiba",    IrProto::NEC,      0,      NEC32(0x02,  0x02)},
    {1,"Panasonic",  IrProto::PANASONIC,0x4004, PAN32(0x01,0x00,0x20)},
    {1,"Sharp",      IrProto::NEC,      0,      NEC32(0x40,  0x1A)},
    {1,"Hisense",    IrProto::NEC,      0,      NEC32(0x00,  0x02)},
    // ---- TV Vol- ----
    {2,"Samsung",    IrProto::NEC,      0,      NEC32S(0x07, 0x0B)},
    {2,"LG",         IrProto::NEC,      0,      NEC32(0x04,  0x03)},
    {2,"Sony",       IrProto::SONY,     12,     0x0C90},
    {2,"Toshiba",    IrProto::NEC,      0,      NEC32(0x02,  0x03)},
    {2,"Panasonic",  IrProto::PANASONIC,0x4004, PAN32(0x01,0x00,0x21)},
    {2,"Sharp",      IrProto::NEC,      0,      NEC32(0x40,  0x1B)},
    {2,"Hisense",    IrProto::NEC,      0,      NEC32(0x00,  0x03)},
    // ---- TV Mute ----
    {3,"Samsung",    IrProto::NEC,      0,      NEC32S(0x07, 0x0F)},
    {3,"LG",         IrProto::NEC,      0,      NEC32(0x04,  0x09)},
    {3,"Sony",       IrProto::SONY,     12,     0x1490},
    {3,"Toshiba",    IrProto::NEC,      0,      NEC32(0x02,  0x09)},
    {3,"Panasonic",  IrProto::PANASONIC,0x4004, PAN32(0x01,0x00,0x4C)},
    {3,"Sharp",      IrProto::NEC,      0,      NEC32(0x40,  0x14)},
    {3,"Hisense",    IrProto::NEC,      0,      NEC32(0x00,  0x09)},
};
static constexpr uint16_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

static void sendPreset(const IrPreset& p) {
    switch (p.proto) {
        case IrProto::NEC:
            g_irsend.sendNEC((uint64_t)p.data, 32);
            break;
        case IrProto::SONY:
            for (int i = 0; i < 3; i++) {
                g_irsend.sendSony((uint64_t)p.data, (uint16_t)p.address);
                delay(40);
            }
            break;
        case IrProto::PANASONIC:
            g_irsend.sendPanasonic((uint16_t)p.address, (uint32_t)p.data);
            break;
        case IrProto::RC5:
            g_irsend.sendRC5((uint64_t)p.data, 12);
            break;
        case IrProto::RC6:
            g_irsend.sendRC6((uint64_t)p.data, 20);
            break;
        case IrProto::SAMSUNG:
            g_irsend.sendSAMSUNG((uint64_t)p.data, 32);
            break;
        case IrProto::JVC:
            g_irsend.sendJVC((uint64_t)p.data, 16);
            break;
    }
}

// ---- Signal storage (in-memory) ---------------------------------------------

static IrSignal s_signals[8];
static uint8_t  s_sigCount  = 0;
static uint8_t  s_replayIdx = 0;

// ---- SD helpers -------------------------------------------------------------

static bool sdInit() {
    if (!spi_mutex) return false;
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    bool ok = SD.begin(PIN_SD_CS);
    xSemaphoreGive(spi_mutex);
    return ok;
}

static bool sdSaveSignal(const IrSignal& sig, uint8_t idx) {
    if (!spi_mutex) return false;
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    if (!SD.exists("/ir")) SD.mkdir("/ir");
    char path[24];
    snprintf(path, sizeof(path), "/ir/%03u.ir", idx);
    File f = SD.open(path, FILE_WRITE);
    bool ok = false;
    if (f) {
        f.printf("proto:%u\naddr:0x%04X\ncmd:0x%04X\nbits:%u\nval:0x%llX\ntag:%s\nrawlen:%u\n",
                 sig.protocol, sig.address, sig.command,
                 sig.bits, sig.value, sig.tag, sig.rawLen);
        if (sig.rawLen > 0) {
            f.print("raw:");
            for (uint16_t i = 0; i < sig.rawLen; i++) {
                f.print(sig.rawBuf[i]);
                if (i + 1 < sig.rawLen) f.print(' ');
            }
            f.print('\n');
        }
        f.close();
        ok = true;
    }
    xSemaphoreGive(spi_mutex);
    return ok;
}

static uint8_t sdLoadSignals() {
    if (!spi_mutex) return 0;
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return 0;

    uint8_t count = 0;
    for (uint8_t idx = 0; idx < 8 && count < 8; idx++) {
        char path[24];
        snprintf(path, sizeof(path), "/ir/%03u.ir", idx);
        File f = SD.open(path);
        if (!f) continue;

        IrSignal& sig = s_signals[count];
        memset(&sig, 0, sizeof(sig));
        snprintf(sig.tag, sizeof(sig.tag), "ir_%03u", idx);

        char line[256];
        while (f.available()) {
            int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            line[n] = '\0';
            if      (strncmp(line, "proto:",  6) == 0) sig.protocol = atoi(line + 6);
            else if (strncmp(line, "addr:",   5) == 0) sig.address  = (uint16_t)strtol(line + 5, nullptr, 16);
            else if (strncmp(line, "cmd:",    4) == 0) sig.command  = (uint16_t)strtol(line + 4, nullptr, 16);
            else if (strncmp(line, "bits:",   5) == 0) sig.bits     = atoi(line + 5);
            else if (strncmp(line, "val:",    4) == 0) sig.value    = strtoull(line + 4, nullptr, 16);
            else if (strncmp(line, "tag:",    4) == 0) strncpy(sig.tag, line + 4, sizeof(sig.tag) - 1);
            else if (strncmp(line, "rawlen:", 7) == 0) sig.rawLen   = atoi(line + 7);
            else if (strncmp(line, "raw:",    4) == 0) {
                char* p = line + 4;
                uint16_t ri = 0;
                while (*p && ri < 256) {
                    sig.rawBuf[ri++] = atoi(p);
                    while (*p && *p != ' ') p++;
                    while (*p == ' ') p++;
                }
                sig.rawLen = ri;
            }
        }
        f.close();
        count++;
    }
    xSemaphoreGive(spi_mutex);
    return count;
}

// ---- helpers ----------------------------------------------------------------

void IrModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

static void playSignal(const IrSignal& sig) {
    if (sig.rawLen > 0) {
        g_irsend.sendRaw(sig.rawBuf, sig.rawLen, 38);
    } else if (sig.bits > 0 && sig.value != 0) {
        g_irsend.send((decode_type_t)sig.protocol, sig.value, sig.bits);
    }
}

// ---- Preset task ------------------------------------------------------------

static void irPresetTask(void* arg) {
    IrModule* self = reinterpret_cast<IrModule*>(arg);
    g_irsend.begin();

    uint8_t cat = self->presetCat_;
    uint32_t sent = 0;
    char buf[48];

    uint16_t idx[kPresetCount];
    uint16_t cnt = 0;
    for (uint16_t i = 0; i < kPresetCount; i++)
        if (kPresets[i].cat == cat) idx[cnt++] = i;

    if (cnt == 0) {
        self->setStatus("No presets");
        while (self->isRunning()) vTaskDelay(pdMS_TO_TICKS(200));
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    static const char* kCatNames[] = {"Power","Vol+","Vol-","Mute"};
    const char* catName = (cat < 4) ? kCatNames[cat] : "?";

    while (self->isRunning()) {
        uint16_t pos = self->presetIdx_ % cnt;
        const IrPreset& p = kPresets[idx[pos]];

        if (self->presetSend_) {
            self->presetSend_ = false;
            for (int r = 0; r < 3; r++) {
                sendPreset(p);
                vTaskDelay(pdMS_TO_TICKS(80));
            }
            sent++;
        }

        snprintf(buf, sizeof(buf), "%s  %u/%u\n%s  %lu sent",
                 p.brand, pos + 1, cnt, catName, (unsigned long)sent);
        self->setStatus(buf);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- TV Kill task -----------------------------------------------------------

static void irTvKillTask(void* arg) {
    IrModule* self = reinterpret_cast<IrModule*>(arg);
    g_irsend.begin();

    uint8_t idx = 0;
    char buf[40];
    while (self->isRunning()) {
        const TvCode& c = kTvCodes[idx];
        snprintf(buf, sizeof(buf), "-> %s", c.brand);
        self->setStatus(buf);
        for (int r = 0; r < 3 && self->isRunning(); r++) {
            g_irsend.sendNEC((uint64_t)c.code, 32);
            vTaskDelay(pdMS_TO_TICKS(120));
        }
        idx = (idx + 1) % kTvCount;
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- Capture task -----------------------------------------------------------

static void irCaptureTask(void* arg) {
    IrModule* self = reinterpret_cast<IrModule*>(arg);
    g_irrecv.enableIRIn();
    self->setStatus("Point remote\n& press button");

    decode_results results;
    char buf[48];

    while (self->isRunning()) {
        if (!g_irrecv.decode(&results)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (s_sigCount >= 8) s_sigCount = 0;
        IrSignal& sig = s_signals[s_sigCount];
        memset(&sig, 0, sizeof(sig));

        sig.protocol = (uint8_t)results.decode_type;
        sig.value    = results.value;
        sig.bits     = results.bits;
        sig.address  = results.address;
        sig.command  = results.command;
        sig.rawLen   = 0;
        snprintf(sig.tag, sizeof(sig.tag), "ir_%03u", s_sigCount);

        bool saved = sdSaveSignal(sig, s_sigCount);
        snprintf(buf, sizeof(buf), "P%u A:%04X C:%04X\n%s",
                 sig.protocol, sig.address, sig.command,
                 saved ? "Saved to SD" : "In memory only");
        self->setStatus(buf);
        s_sigCount++;

        g_irrecv.resume();
        vTaskDelay(pdMS_TO_TICKS(1500));
        self->setStatus("Point remote\n& press button");
    }

    g_irrecv.disableIRIn();
    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- Replay task ------------------------------------------------------------

static void irReplayTask(void* arg) {
    IrModule* self = reinterpret_cast<IrModule*>(arg);
    g_irsend.begin();

    bool sdOk = sdInit();
    if (sdOk) {
        uint8_t loaded = sdLoadSignals();
        if (loaded > 0) s_sigCount = loaded;
    }

    if (s_sigCount == 0) {
        self->setStatus("No signals\nCapture first");
        while (self->isRunning()) vTaskDelay(pdMS_TO_TICKS(200));
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    s_replayIdx = 0;
    char buf[48];

    while (self->isRunning()) {
        const IrSignal& sig = s_signals[s_replayIdx];
        snprintf(buf, sizeof(buf), "%s  %u/%u\nP%u A:%04X C:%04X",
                 sig.tag, s_replayIdx + 1, s_sigCount,
                 sig.protocol, sig.address, sig.command);
        self->setStatus(buf);

        playSignal(sig);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- IModule ----------------------------------------------------------------

void IrModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    presetIdx_  = 0;
    presetSend_ = false;
    TaskFunction_t fn = nullptr;
    switch (mode_) {
        case IrMode::TV_KILL: fn = irTvKillTask;  break;
        case IrMode::CAPTURE: fn = irCaptureTask; break;
        case IrMode::REPLAY:  fn = irReplayTask;  break;
        case IrMode::PRESET:  fn = irPresetTask;  break;
    }
    xTaskCreatePinnedToCore(fn, "ir", 6144, this, 1, &task_, 1);
}

void IrModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 60 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void IrModule::onEvent(uint8_t ev) {
    if (mode_ == IrMode::PRESET) {
        if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT))
            presetIdx_++;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT))
            presetIdx_ = (presetIdx_ > 0) ? presetIdx_ - 1 : 0;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_OK))
            presetSend_ = true;
    } else if (mode_ == IrMode::REPLAY && s_sigCount > 0) {
        if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT))
            s_replayIdx = (s_replayIdx + 1) % s_sigCount;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT))
            s_replayIdx = (s_replayIdx + (uint8_t)s_sigCount - 1) % s_sigCount;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_OK))
            playSignal(s_signals[s_replayIdx]);
    }
}

void IrModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
