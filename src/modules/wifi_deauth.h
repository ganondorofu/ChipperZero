#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

enum class DeauthMode  : uint8_t { RANDOM, TARGETED };
enum class DeauthState : uint8_t { SCANNING, SELECTING, ATTACKING };

class DeauthModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "Deauth"; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void incSent()         { sent_.fetch_add(1); }
    void setApCount(uint8_t n) { apCount_.store(n); }
    void setMode(DeauthMode m) { mode_ = m; }
    uint32_t getSent() const   { return sent_.load(); }

    DeauthState state_  = DeauthState::SCANNING;
    uint8_t     scroll_ = 0;
    portMUX_TYPE mux_   = portMUX_INITIALIZER_UNLOCKED;

private:
    std::atomic<bool>     running_{false};
    std::atomic<uint32_t> sent_{0};
    std::atomic<uint8_t>  apCount_{0};
    DeauthMode            mode_  = DeauthMode::RANDOM;
    TaskHandle_t          task_  = nullptr;
    char                  status_[48] = "";

public:
    void setStatus(const char* s);
    void fillStatusBuf(char* buf, size_t len);
};

extern DeauthModule g_wifiDeauth;
