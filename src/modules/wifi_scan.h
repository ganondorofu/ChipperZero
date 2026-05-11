#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class WifiScanModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    const char* name() override { return "WiFi Scan"; }
    void fillStats(char* buf, size_t len) override;
    void onEvent(uint8_t ev) override;

    bool isRunning() const { return running_.load(); }
    void clearTask() { task_ = nullptr; }

private:
    std::atomic<bool> running_{false};
    TaskHandle_t task_ = nullptr;
};

extern WifiScanModule g_wifiScan;
