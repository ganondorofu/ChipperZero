#include "wifi_manager.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

WifiManagerModule g_wifiManager;

static const char AP_SSID[] = "ChipperZero";
static const char AP_PASS[] = "chipperzero";
static const char NVS_NS[]  = "wifi_mgr";

bool WifiManagerModule::init() {
    Preferences prefs;
    prefs.begin(NVS_NS, true);
    mode_ = static_cast<WifiMgrMode>(prefs.getUChar("mode", 0));
    prefs.getString("ssid", ssid_, sizeof(ssid_));
    prefs.getString("pass", pass_, sizeof(pass_));
    prefs.end();
    if (mode_ != WifiMgrMode::OFF) applyWifi();
    return true;
}

void WifiManagerModule::start() {
    applyWifi();
}

void WifiManagerModule::stop() {
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}

void WifiManagerModule::configure(WifiMgrMode mode, const char* ssid, const char* pass) {
    mode_ = mode;
    if (ssid) {
        strncpy(ssid_, ssid, sizeof(ssid_) - 1);
        ssid_[sizeof(ssid_) - 1] = '\0';
    }
    if (pass && pass[0]) {
        strncpy(pass_, pass, sizeof(pass_) - 1);
        pass_[sizeof(pass_) - 1] = '\0';
    }

    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putUChar("mode", static_cast<uint8_t>(mode_));
    prefs.putString("ssid", ssid_);
    prefs.putString("pass", pass_);
    prefs.end();

    applyWifi();
}

void WifiManagerModule::fillStats(char* buf, size_t len) {
    switch (mode_) {
        case WifiMgrMode::AP: {
            IPAddress ip = WiFi.softAPIP();
            snprintf(buf, len, "AP\n%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            break;
        }
        case WifiMgrMode::STA:
            if (WiFi.status() == WL_CONNECTED) {
                IPAddress ip = WiFi.localIP();
                snprintf(buf, len, "STA\n%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            } else {
                snprintf(buf, len, "STA\nConnecting...");
            }
            break;
        case WifiMgrMode::APSTA: {
            IPAddress ip = WiFi.softAPIP();
            snprintf(buf, len, "AP+STA\n%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            break;
        }
        default:
            snprintf(buf, len, "Off\n-");
            break;
    }
}

void WifiManagerModule::applyWifi() {
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(100);

    switch (mode_) {
        case WifiMgrMode::AP:
            WiFi.mode(WIFI_AP);
            WiFi.softAP(AP_SSID, AP_PASS);
            break;
        case WifiMgrMode::STA:
            WiFi.mode(WIFI_STA);
            if (ssid_[0]) WiFi.begin(ssid_, pass_[0] ? pass_ : nullptr);
            break;
        case WifiMgrMode::APSTA:
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(AP_SSID, AP_PASS);
            if (ssid_[0]) WiFi.begin(ssid_, pass_[0] ? pass_ : nullptr);
            break;
        default:
            WiFi.mode(WIFI_OFF);
            break;
    }
}
