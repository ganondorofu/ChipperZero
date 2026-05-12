#include "ir.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <IRremote.hpp>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "../hal/encoder.h"
#include "../hal/pins.h"

IrModule g_ir;

extern SemaphoreHandle_t spi_mutex;

// ---- TV Kill codes (NEC fast-cycle) -----------------------------------------
struct TvCode { uint16_t addr; uint8_t cmd; const char* brand; };

static const TvCode kTvCodes[] = {
    { 0x0707, 0x02, "Samsung"   },
    { 0x0004, 0x08, "LG"        },
    { 0x0001, 0x15, "Sony"      },
    { 0x4004, 0x3D, "Panasonic" },
    { 0x4004, 0x08, "Sharp"     },
    { 0x0000, 0x0C, "Philips"   },
    { 0x02FD, 0x00, "Toshiba"   },
    { 0xD059, 0x00, "Vizio"     },
    { 0x0000, 0x08, "Hisense"   },
    { 0x0100, 0x4D, "TCL"       },
    { 0x0154, 0x00, "Hitachi"   },
    { 0x0090, 0x28, "Haier"     },
};
static constexpr uint8_t kTvCount = sizeof(kTvCodes) / sizeof(kTvCodes[0]);

// ---- Preset database ---------------------------------------------------------
// cat: 0=TV Power  1=TV Vol+  2=TV Vol-  3=TV Mute

static const IrPreset kPresets[] = {
    // ---- TV Power ----
    {0,"Samsung",   IrProto::NEC,      0x0707, 0x02},
    {0,"Samsung B", IrProto::NEC,      0x0400, 0x02},
    {0,"LG",        IrProto::NEC,      0x0004, 0x08},
    {0,"LG B",      IrProto::NEC,      0x0100, 0x08},
    {0,"Sony",      IrProto::SONY,     0x0001, 0x15},
    {0,"Sony B",    IrProto::SONY,     0x0001, 0x2E},
    {0,"Panasonic", IrProto::PANASONIC,0x4004, 0x3D},
    {0,"Sharp",     IrProto::NEC,      0x4004, 0x08},
    {0,"Philips",   IrProto::RC6,      0x0000, 0x0C},
    {0,"Toshiba",   IrProto::NEC,      0x02FD, 0x00},
    {0,"Hisense",   IrProto::NEC,      0x0000, 0x08},
    {0,"TCL",       IrProto::NEC,      0x0100, 0x4D},
    {0,"Vizio",     IrProto::NEC,      0xD059, 0x00},
    {0,"Hitachi",   IrProto::NEC,      0x0154, 0x00},
    {0,"JVC",       IrProto::JVC,      0xC5E8, 0x00},
    {0,"Haier",     IrProto::NEC,      0x0090, 0x28},
    {0,"Mitsubishi",IrProto::NEC,      0x0A8B, 0x1E},
    {0,"Funai",     IrProto::NEC,      0x0402, 0x08},
    {0,"Insignia",  IrProto::NEC,      0x0000, 0x08},
    {0,"Sanyo",     IrProto::NEC,      0x0110, 0x1A},
    // ---- TV Vol+ ----
    {1,"Samsung",   IrProto::NEC,      0x0707, 0x07},
    {1,"LG",        IrProto::NEC,      0x0004, 0x02},
    {1,"Sony",      IrProto::SONY,     0x0001, 0x12},
    {1,"Panasonic", IrProto::PANASONIC,0x4004, 0x20},
    {1,"Philips",   IrProto::RC6,      0x0000, 0x10},
    {1,"Toshiba",   IrProto::NEC,      0x02FD, 0x02},
    {1,"Sharp",     IrProto::NEC,      0x4004, 0x1A},
    {1,"Hisense",   IrProto::NEC,      0x0000, 0x02},
    // ---- TV Vol- ----
    {2,"Samsung",   IrProto::NEC,      0x0707, 0x0B},
    {2,"LG",        IrProto::NEC,      0x0004, 0x03},
    {2,"Sony",      IrProto::SONY,     0x0001, 0x13},
    {2,"Panasonic", IrProto::PANASONIC,0x4004, 0x21},
    {2,"Philips",   IrProto::RC6,      0x0000, 0x11},
    {2,"Toshiba",   IrProto::NEC,      0x02FD, 0x03},
    {2,"Sharp",     IrProto::NEC,      0x4004, 0x1B},
    {2,"Hisense",   IrProto::NEC,      0x0000, 0x03},
    // ---- TV Mute ----
    {3,"Samsung",   IrProto::NEC,      0x0707, 0x0F},
    {3,"LG",        IrProto::NEC,      0x0004, 0x09},
    {3,"Sony",      IrProto::SONY,     0x0001, 0x14},
    {3,"Panasonic", IrProto::PANASONIC,0x4004, 0x4C},
    {3,"Philips",   IrProto::RC6,      0x0000, 0x0F},
    {3,"Toshiba",   IrProto::NEC,      0x02FD, 0x09},
    {3,"Sharp",     IrProto::NEC,      0x4004, 0x14},
    {3,"Hisense",   IrProto::NEC,      0x0000, 0x09},
};
static constexpr uint16_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

static void sendPreset(const IrPreset& p) {
    switch (p.proto) {
        case IrProto::NEC:       IrSender.sendNEC(p.address, (uint8_t)p.command, 0);       break;
        case IrProto::SONY:      IrSender.sendSony(p.address, (uint8_t)p.command, 0);      break;
        case IrProto::RC5:       IrSender.sendRC5(p.address, (uint8_t)p.command, 0);       break;
        case IrProto::RC6:       IrSender.sendRC6(p.address, (uint8_t)p.command, 0);       break;
        case IrProto::SAMSUNG:   IrSender.sendSamsung(p.address, (uint8_t)p.command, 0);   break;
        case IrProto::PANASONIC: IrSender.sendPanasonic(p.address, (uint8_t)p.command, 0); break;
        case IrProto::JVC:       IrSender.sendJVC(p.address, (uint8_t)p.command, 0);       break;
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
        f.printf("proto:%u\naddr:0x%04X\ncmd:0x%02X\ntag:%s\nrawlen:%u\n",
                 sig.protocol, sig.address, sig.command, sig.tag, sig.rawLen);
        f.print("raw:");
        for (uint16_t i = 0; i < sig.rawLen; i++) {
            f.print(sig.rawBuf[i]);
            if (i + 1 < sig.rawLen) f.print(' ');
        }
        f.print('\n');
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
            if (strncmp(line, "proto:", 6) == 0)  sig.protocol = atoi(line + 6);
            else if (strncmp(line, "addr:", 5) == 0)
                sig.address = (uint16_t)strtol(line + 5, nullptr, 16);
            else if (strncmp(line, "cmd:", 4) == 0)
                sig.command = (uint8_t)strtol(line + 4, nullptr, 16);
            else if (strncmp(line, "tag:", 4) == 0)
                strncpy(sig.tag, line + 4, sizeof(sig.tag) - 1);
            else if (strncmp(line, "rawlen:", 7) == 0)
                sig.rawLen = atoi(line + 7);
            else if (strncmp(line, "raw:", 4) == 0) {
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
        IrSender.sendRaw(sig.rawBuf, sig.rawLen, 38);
    } else {
        switch (sig.protocol) {
            case 3:  IrSender.sendNEC(sig.address, sig.command, 0);    break; // NEC
            case 7:  IrSender.sendSony(sig.address, sig.command, 0);   break; // SONY
            case 11: IrSender.sendRC5(sig.address, sig.command, 0);    break; // RC5
            case 13: IrSender.sendRC6(sig.address, sig.command, 0);    break; // RC6
            case 4:  IrSender.sendSamsung(sig.address, sig.command, 0);break; // SAMSUNG
            default: break;
        }
    }
}

// ---- Preset task ------------------------------------------------------------

static void irPresetTask(void* arg) {
    IrModule* self = reinterpret_cast<IrModule*>(arg);
    IrSender.begin(PIN_IR_TX, false);

    uint8_t cat = self->presetCat_;
    uint32_t sent = 0;
    char buf[48];

    // build index of presets matching this category
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
                vTaskDelay(pdMS_TO_TICKS(100));
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
    IrSender.begin(PIN_IR_TX, false);

    uint8_t idx = 0;
    char buf[40];
    while (self->isRunning()) {
        const TvCode& c = kTvCodes[idx];
        snprintf(buf, sizeof(buf), "-> %s", c.brand);
        self->setStatus(buf);
        for (int r = 0; r < 3 && self->isRunning(); r++) {
            IrSender.sendNEC(c.addr, c.cmd, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        idx = (idx + 1) % kTvCount;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- Capture task -----------------------------------------------------------

static void irCaptureTask(void* arg) {
    IrModule* self = reinterpret_cast<IrModule*>(arg);
    IrReceiver.begin(PIN_IR_RX, false);
    self->setStatus("Point remote\n& press button");

    while (self->isRunning()) {
        if (!IrReceiver.decode()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        if (s_sigCount >= 8) s_sigCount = 0;  // ring buffer
        IrSignal& sig = s_signals[s_sigCount];
        memset(&sig, 0, sizeof(sig));

        sig.protocol = (uint8_t)IrReceiver.decodedIRData.protocol;
        sig.address  = IrReceiver.decodedIRData.address;
        sig.command  = IrReceiver.decodedIRData.command;
        sig.rawLen   = 0;  // decoded mode; raw replay uses protocol-specific sender

        snprintf(sig.tag, sizeof(sig.tag), "ir_%03u", s_sigCount);

        char buf[48];
        bool saved = sdSaveSignal(sig, s_sigCount);
        snprintf(buf, sizeof(buf), "P%u A:%04X C:%02X\n%s",
                 sig.protocol, sig.address, sig.command,
                 saved ? "Saved to SD" : "In memory only");
        self->setStatus(buf);

        s_sigCount++;
        IrReceiver.resume();
        vTaskDelay(pdMS_TO_TICKS(1500));
        self->setStatus("Point remote\n& press button");
    }

    IrReceiver.stop();
    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- Replay task ------------------------------------------------------------

static void irReplayTask(void* arg) {
    IrModule* self = reinterpret_cast<IrModule*>(arg);
    IrSender.begin(PIN_IR_TX, false);

    // Load from SD first
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
        snprintf(buf, sizeof(buf), "%s  %u/%u\nP%u A:%04X C:%02X",
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
    xTaskCreatePinnedToCore(fn, "ir", 6144, this, 1, &task_, 0);
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
