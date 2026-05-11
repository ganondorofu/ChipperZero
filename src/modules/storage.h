#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

class StorageModule : public IModule {
public:
    bool init() override { return false; }   // SD card not connected yet
    bool isAvailable() override { return false; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return "Storage"; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void setStatus(const char* s);
    void scrollDown()      { scroll_++; }
    void scrollUp()        { if (scroll_ > 0) scroll_--; }
    uint8_t scroll() const { return scroll_; }

private:
    std::atomic<bool> running_{false};
    TaskHandle_t      task_   = nullptr;
    char              status_[48] = "Ready";
    uint8_t           scroll_ = 0;
    portMUX_TYPE      mux_    = portMUX_INITIALIZER_UNLOCKED;
};

extern StorageModule g_storage;
