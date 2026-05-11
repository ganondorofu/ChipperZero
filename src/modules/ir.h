#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

enum class IrMode : uint8_t { TV_KILL, CAPTURE, REPLAY };

// ---- IrSignal: one captured/loaded signal -----------------------------------
struct IrSignal {
    uint8_t  protocol;   // decode_type_t cast to uint8_t; 0 = raw
    uint16_t address;
    uint8_t  command;
    uint16_t rawBuf[256];
    uint16_t rawLen;
    char     tag[16];    // label shown in fillStats
};

class IrModule : public IModule {
public:
    bool init() override { return false; }
    bool isAvailable() override { return false; }  // TX hardware not connected yet
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return name_; }

    void setMode(IrMode m, const char* n) { mode_ = m; name_ = n; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void setStatus(const char* s);

private:
    std::atomic<bool> running_{false};
    TaskHandle_t      task_   = nullptr;
    IrMode            mode_   = IrMode::TV_KILL;
    const char*       name_   = "IR";
    char              status_[48] = "Ready";
    portMUX_TYPE      mux_    = portMUX_INITIALIZER_UNLOCKED;
};

extern IrModule g_ir;
