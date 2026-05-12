#pragma once
#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "module_base.h"

enum class MjState : uint8_t { SCANNING, LOCKED, INJECTING };

class NrfMousejackModule : public IModule {
public:
    bool init() override;
    bool isAvailable() override;
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "MouseJack"; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void setStatus(const char* s);
    void triggerInject()   { doInject_ = true; }
    void setCustomText(const char* t) {
        strncpy(customText_, t, sizeof(customText_) - 1);
        customText_[sizeof(customText_) - 1] = '\0';
    }
    const char* getCustomText() const { return customText_; }

    MjState      state_      = MjState::SCANNING;
    uint8_t      payloadIdx_ = 0;
    volatile bool doInject_  = false;
    portMUX_TYPE mux_        = portMUX_INITIALIZER_UNLOCKED;

private:
    std::atomic<bool> running_{false};
    bool              available_ = false;
    TaskHandle_t      task_      = nullptr;
    char              status_[48] = "";
    char              customText_[33] = {};
};

extern NrfMousejackModule g_nrfMousejack;
