#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class WifiSpectrumModule : public IModule {
public:
    bool init()        override { return true; }
    bool isAvailable() override { return true; }
    void start()       override;
    void stop()        override;
    void onEvent(uint8_t) override {}
    void fillStats(char*, size_t) override {}
    const char* name() override { return "WiFi Spectrum"; }

    bool hasCustomDraw() override { return true; }
    void draw()          override;

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }

    // Written by task (Core 0), read by draw() (Core 1). Volatile is sufficient
    // for a display — a torn read just causes one noisy frame.
    volatile uint16_t counts[13] = {};  // management frame count per channel
    volatile uint8_t  curChan    = 0;   // 0-12, channel being sampled

private:
    std::atomic<bool> running_{false};
    TaskHandle_t      task_ = nullptr;
};

extern WifiSpectrumModule g_wifiSpectrum;
