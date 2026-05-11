#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class NrfSpamModule : public IModule {
public:
    bool init() override;
    bool isAvailable() override;
    void start() override;
    void stop() override;
    const char* name() override { return "NRF Spam"; }

    bool isRunning() const;
    bool hasChip() const { return detected_.load(); }
    void clearTask() { task_ = nullptr; }

private:
    std::atomic<bool> available_{false};
    std::atomic<bool> detected_{false};
    std::atomic<bool> running_{false};
    TaskHandle_t task_ = nullptr;
};

extern NrfSpamModule g_nrfSpam;
