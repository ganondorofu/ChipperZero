#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ble_spam_types.h"
#include "module_base.h"

class BleSpamModule : public IModule {
public:
    bool init() override;
    bool isAvailable() override;
    void start() override;
    void stop() override;
    const char* name() override { return "ESP BLE Spam"; }

    bool isRunning() const { return running_.load(); }
    void clearTask() { task_ = nullptr; }
    void updateAdvsPerSec(uint32_t v) { adv_per_sec_.store(v); }

    void         setType(BleSpamType t) { type_ = static_cast<uint8_t>(t); }
    BleSpamType  getType()        const { return static_cast<BleSpamType>(type_.load()); }

    void fillStats(char* buf, size_t len) override;

private:
    std::atomic<bool>     running_{false};
    std::atomic<uint8_t>  type_{0};  // BleSpamType::ALL
    std::atomic<uint32_t> adv_per_sec_{0};
    TaskHandle_t          task_ = nullptr;
};

extern BleSpamModule g_bleSpam;
