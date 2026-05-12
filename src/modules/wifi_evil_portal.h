#pragma once
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "module_base.h"

enum class PortalState : uint8_t { SCANNING, SELECTING, RUNNING };

class WifiEvilPortalModule : public IModule {
public:
    bool init() override { return true; }
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "Evil Portal"; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void setStatus(const char* s);
    void confirm();

    PortalState  state_   = PortalState::SCANNING;
    uint8_t      scroll_  = 0;
    portMUX_TYPE mux_     = portMUX_INITIALIZER_UNLOCKED;

private:
    std::atomic<bool>     running_{false};
    TaskHandle_t          task_   = nullptr;
    char                  status_[48] = "";
};

extern WifiEvilPortalModule g_wifiEvilPortal;
