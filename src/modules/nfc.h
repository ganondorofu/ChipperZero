#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "module_base.h"

enum class NfcMode : uint8_t { READ, WRITE, EMULATE };

// ---- NfcCard: one read/loaded card ------------------------------------------
struct NfcCard {
    uint8_t  uid[7];
    uint8_t  uidLen;
    bool     isClassic;    // true = Mifare Classic 1K, false = Ultralight
    uint8_t  data[1024];   // 1024 = Classic 1K max; Ultralight uses first 64 bytes
    uint16_t dataLen;
    char     tag[16];
};

class NfcModule : public IModule {
public:
    bool init() override;
    bool isAvailable() override { return available_; }
    void start() override;
    void stop() override;
    void onEvent(uint8_t ev) override;
    void fillStats(char* buf, size_t len) override;
    const char* name() override { return name_; }

    void setMode(NfcMode m, const char* n) { mode_ = m; name_ = n; }

    bool isRunning() const { return running_.load(); }
    void clearTask()       { task_ = nullptr; }
    void setStatus(const char* s);

private:
    bool              available_ = false;
    std::atomic<bool> running_{false};
    TaskHandle_t      task_   = nullptr;
    NfcMode           mode_   = NfcMode::READ;
    const char*       name_   = "NFC";
    char              status_[48] = "Ready";
    portMUX_TYPE      mux_    = portMUX_INITIALIZER_UNLOCKED;
};

extern NfcModule g_nfc;
