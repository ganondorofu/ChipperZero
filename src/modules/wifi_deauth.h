#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class DeauthModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    const char* name() override { return "Deauth"; }
    void fillStats(char* buf, size_t len) override;

    bool isRunning() const { return running_.load(); }
    void clearTask() { task_ = nullptr; }
    void incSent()   { sent_.fetch_add(1); }
    void setApCount(uint8_t n) { apCount_.store(n); }

private:
    std::atomic<bool>    running_{false};
    std::atomic<uint32_t> sent_{0};
    std::atomic<uint8_t>  apCount_{0};
    TaskHandle_t          task_ = nullptr;
};

extern DeauthModule g_wifiDeauth;
