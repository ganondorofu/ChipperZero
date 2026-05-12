#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class NrfJammerModule : public IModule {
public:
    bool init() override;
    bool isAvailable() override;
    void start() override;
    void stop() override;
    void onEvent(uint8_t) override {}
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "BT Jammer"; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> available_{false};
    TaskHandle_t      task_ = nullptr;
};

extern NrfJammerModule g_nrfJammer;
