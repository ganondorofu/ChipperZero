#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class BleScanModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "BLE Scanner"; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void setStatus(const char* s);

    uint8_t      scroll_ = 0;   // accessed from task
    portMUX_TYPE mux_    = portMUX_INITIALIZER_UNLOCKED;

private:
    std::atomic<bool> running_{false};
    TaskHandle_t      task_    = nullptr;
    char              status_[48] = "Scanning...";
};

extern BleScanModule g_bleScan;
