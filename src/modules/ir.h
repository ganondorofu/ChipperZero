#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

enum class IrMode  : uint8_t { TV_KILL, CAPTURE, REPLAY, PRESET };
enum class IrProto : uint8_t { NEC, SONY, RC5, RC6, SAMSUNG, PANASONIC, JVC };

// ---- IrPreset: built-in code entry ------------------------------------------
struct IrPreset {
    uint8_t     cat;      // 0=TV Power, 1=TV Vol+, 2=TV Vol-, 3=TV Mute
    const char* brand;
    IrProto     proto;
    uint16_t    address;
    uint16_t    command;
};

// ---- IrSignal: one captured/loaded signal -----------------------------------
struct IrSignal {
    uint8_t  protocol;   // decode_type_t cast to uint8_t; 0 = raw
    uint16_t address;
    uint8_t  command;
    uint16_t rawBuf[256];
    uint16_t rawLen;
    char     tag[16];
};

class IrModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return name_; }

    void setMode(IrMode m, const char* n) { mode_ = m; name_ = n; }
    void setPresetCat(uint8_t cat) { presetCat_ = cat; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void setStatus(const char* s);

    // accessed from preset task
    uint8_t  presetCat_ = 0;
    uint16_t presetIdx_ = 0;
    volatile bool presetSend_ = false;

private:
    std::atomic<bool> running_{false};
    TaskHandle_t      task_   = nullptr;
    IrMode            mode_   = IrMode::TV_KILL;
    const char*       name_   = "IR";
    char              status_[48] = "Ready";
    portMUX_TYPE      mux_    = portMUX_INITIALIZER_UNLOCKED;
};

extern IrModule g_ir;
