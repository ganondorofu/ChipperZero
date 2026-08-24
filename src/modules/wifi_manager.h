#pragma once

#include "module_base.h"
#include <DNSServer.h>
#include <WebServer.h>

enum class WifiMgrMode : uint8_t { OFF = 0, AP = 1, STA = 2, APSTA = 3 };

class WifiManagerModule : public IModule {
public:
    bool init() override;
    bool isAvailable() override { return true; }
    void start() override;
    void stop() override;
    const char* name() override { return "WiFi Manager"; }
    void fillStats(char* buf, size_t len) override;

    void configure(WifiMgrMode mode, const char* ssid, const char* pass);
    WifiMgrMode getMode() const { return mode_; }
    const char* getSsid() const { return ssid_; }

    // Call from loop() to service HTTP requests.
    void handleClient();

private:
    void applyWifi();
    void startServer();
    void stopServer();

    WifiMgrMode mode_ = WifiMgrMode::OFF;
    char ssid_[33] = {};
    char pass_[64] = {};
    WebServer server_{80};
    DNSServer dns_;
    bool serverRunning_ = false;
    bool dnsRunning_ = false;
};

extern WifiManagerModule g_wifiManager;
