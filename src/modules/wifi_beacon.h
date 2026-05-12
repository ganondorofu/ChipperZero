#pragma once

#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class BeaconSpamModule : public IModule {
public:
    bool init() override;
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "Beacon Spam"; }

    bool isRunning() const { return running_.load(); }
    void clearTask() { task_ = nullptr; }
    void setRate(uint32_t r) { rate_.store(r); }
    void incSent() { sent_.fetch_add(1); }

    void setCount(uint16_t c)   { count_ = c; }
    void setPattern(uint8_t p)  { pattern_.store(p); }
    uint8_t  getPattern() const { return pattern_.load(); }
    uint16_t getCount()   const { return count_; }
    uint32_t getSent()    const { return sent_.load(); }

    void setCustomSSID(const char* s);  // also persists to NVS
    bool        useCustom()  const { return useCustom_; }
    const char* customSSID() const { return customSSID_; }

    std::atomic<uint32_t> sent_{0};  // accessed from task

private:
    std::atomic<bool>     running_{false};
    std::atomic<uint32_t> rate_{0};
    std::atomic<uint8_t>  pattern_{0};
    uint16_t              count_      = 0;
    bool                  useCustom_  = false;
    char                  customSSID_[33] = {};
    TaskHandle_t          task_ = nullptr;
};

extern BeaconSpamModule g_wifiBeacon;
