#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

enum class CloneState : uint8_t { SCANNING, SELECTING, CLONING };

class BeaconCloneModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "Beacon Clone"; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void setStatus(const char* s);
    void confirmSelect();

    CloneState   state_  = CloneState::SCANNING;
    uint8_t      scroll_ = 0;
    portMUX_TYPE mux_    = portMUX_INITIALIZER_UNLOCKED;

private:
    std::atomic<bool>     running_{false};
    std::atomic<uint32_t> sent_{0};
    TaskHandle_t          task_   = nullptr;
    char                  status_[48] = "";
};

extern BeaconCloneModule g_wifiBeaconClone;
