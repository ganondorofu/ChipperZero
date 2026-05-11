#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class WifiSnifferModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "WiFi Sniffer"; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }

    void addMgmt() { mgmt_.fetch_add(1); }
    void addData() { data_.fetch_add(1); }
    void addCtrl() { ctrl_.fetch_add(1); }

    uint8_t channel_ = 1;  // accessed from task

private:
    std::atomic<bool>     running_{false};
    TaskHandle_t          task_   = nullptr;
    std::atomic<uint32_t> mgmt_{0};
    std::atomic<uint32_t> data_{0};
    std::atomic<uint32_t> ctrl_{0};
};

extern WifiSnifferModule g_wifiSniffer;
