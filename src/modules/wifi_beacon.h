#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class BeaconSpamModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    const char* name() override { return "Beacon Spam"; }
    void fillStats(char* buf, size_t len) override;

    bool isRunning() const { return running_.load(); }
    void clearTask() { task_ = nullptr; }
    void setRate(uint32_t r) { rate_.store(r); }

private:
    std::atomic<bool>     running_{false};
    std::atomic<uint32_t> rate_{0};
    TaskHandle_t          task_ = nullptr;
};

extern BeaconSpamModule g_wifiBeacon;
