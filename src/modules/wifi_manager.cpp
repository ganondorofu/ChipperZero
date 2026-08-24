#include "wifi_manager.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include "../ui/menu.h"
#include "ble_scan.h"
#include "ble_spam.h"
#include "ir.h"
#include "nfc.h"
#include "nrf_ble_spam.h"
#include "nrf_jammer.h"
#include "nrf_mousejack.h"
#include "nrf_spam.h"
#include "wifi_beacon.h"
#include "wifi_beacon_clone.h"
#include "wifi_deauth.h"
#include "wifi_evil_portal.h"
#include "wifi_evil_twin.h"
#include "wifi_scan.h"
#include "wifi_sniffer.h"
#include "wifi_spectrum.h"

WifiManagerModule g_wifiManager;

static const char AP_SSID[] = "ChipperZero";
static const char AP_PASS[] = "chipperzero";
static const char NVS_NS[]  = "wifi_mgr";

// ---- Module registry (name → pointer) ----------------------------------------

struct ModEntry { const char* name; IModule* mod; };
static const ModEntry kMods[] = {
    {"NRF Spam",      &g_nrfSpam},
    {"NRF BLE Spam",  &g_nrfBleSpam},
    {"BT Jammer",     &g_nrfJammer},
    {"MouseJack",     &g_nrfMousejack},
    {"ESP BLE Spam",  &g_bleSpam},
    {"BLE Scanner",   &g_bleScan},
    {"WiFi Scan",     &g_wifiScan},
    {"Deauth",        &g_wifiDeauth},
    {"Beacon Spam",   &g_wifiBeacon},
    {"WiFi Sniffer",  &g_wifiSniffer},
    {"Evil Twin",     &g_wifiEvilTwin},
    {"Evil Portal",   &g_wifiEvilPortal},
    {"Beacon Clone",  &g_wifiBeaconClone},
    {"WiFi Spectrum", &g_wifiSpectrum},
    {"NFC",           &g_nfc},
    {"IR",            &g_ir},
};
static constexpr uint8_t kModCount = sizeof(kMods) / sizeof(kMods[0]);

static IModule* findMod(const String& name) {
    for (uint8_t i = 0; i < kModCount; i++)
        if (name == kMods[i].name) return kMods[i].mod;
    return nullptr;
}

// ---- Embedded WebUI ----------------------------------------------------------

static const char kHtml[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ChipperZero</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#111;color:#eee;font:14px/1.5 sans-serif;padding:12px}
h1{color:#4fc3f7;font-size:20px;margin-bottom:10px}
.card{background:#1e1e2e;border-radius:8px;padding:10px;margin-bottom:8px}
.st{color:#81c784;font-size:13px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-top:8px}
button{background:#283593;color:#eee;border:none;border-radius:6px;
  padding:9px 4px;cursor:pointer;font-size:12px;width:100%;text-align:center}
button.on{background:#b71c1c}
button:disabled{background:#2a2a2a;color:#555;cursor:default}
</style>
</head><body>
<h1>ChipperZero</h1>
<div class="card"><div id="st" class="st">接続中...</div></div>
<div class="card">
<b>モジュール</b>
<div class="grid" id="mg"></div>
</div>
<script>
async function poll(){
try{
const d=await(await fetch('/api/status')).json();
document.getElementById('st').innerHTML=
  '<b>Active: '+(d.active||'なし')+'</b><br>WiFi: '+d.wifi;
const g=document.getElementById('mg');
g.innerHTML=d.mods.map(m=>
  '<button onclick="cmd(\''+m.n+'\')"'+(d.active===m.n?' class="on"':'')+(m.a?'':' disabled')+'>'+
  (d.active===m.n?'&#9632; ':'&#9654; ')+m.n+'</button>'
).join('');
}catch(e){document.getElementById('st').textContent='Error: '+e;}
}
async function cmd(n){
try{
await fetch('/api/cmd',{method:'POST',
  headers:{'Content-Type':'text/plain'},body:n});
}catch(e){}
setTimeout(poll,400);
}
poll();setInterval(poll,2000);
</script>
</body></html>
)html";

// ---- HTTP handlers -----------------------------------------------------------

void WifiManagerModule::startServer() {
    if (serverRunning_) return;

    server_.on("/", HTTP_GET, [this]() {
        server_.send_P(200, "text/html", kHtml);
    });

    // Captive portal: redirect any unknown path to root
    server_.onNotFound([this]() {
        server_.sendHeader("Location", "http://192.168.4.1/", true);
        server_.send(302, "text/plain", "");
    });

    server_.on("/api/status", HTTP_GET, [this]() {
        IModule* active = menu::activeModule();
        const char* activeName = active ? active->name() : "";

        char wifiBuf[32];
        fillStats(wifiBuf, sizeof(wifiBuf));
        // replace \n with space for JSON
        for (char* p = wifiBuf; *p; p++) if (*p == '\n') *p = ' ';

        String json = "{\"active\":\"";
        json += activeName;
        json += "\",\"wifi\":\"";
        json += wifiBuf;
        json += "\",\"mods\":[";
        for (uint8_t i = 0; i < kModCount; i++) {
            if (i > 0) json += ',';
            json += "{\"n\":\"";
            json += kMods[i].name;
            json += "\",\"a\":";
            json += kMods[i].mod->isAvailable() ? "true" : "false";
            json += '}';
        }
        json += "]}";
        server_.send(200, "application/json", json);
    });

    server_.on("/api/cmd", HTTP_POST, [this]() {
        String modName = server_.arg("plain");
        modName.trim();
        IModule* m = findMod(modName);
        if (!m) { server_.send(404, "text/plain", "Unknown module"); return; }

        IModule* active = menu::activeModule();
        if (active == m) {
            menu::requestStop();
        } else {
            menu::requestLaunch(m);
        }
        server_.send(200, "text/plain", "OK");
    });

    server_.begin();
    serverRunning_ = true;
}

void WifiManagerModule::stopServer() {
    if (dnsRunning_)    { dns_.stop();    dnsRunning_    = false; }
    if (serverRunning_) { server_.stop(); serverRunning_ = false; }
}


void WifiManagerModule::handleClient() {
    if (dnsRunning_)    dns_.processNextRequest();
    if (serverRunning_) server_.handleClient();
}

// ---- IModule ----------------------------------------------------------------

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
    // WiFi is already running from init()/configure(). Nothing to do here.
}

void WifiManagerModule::stop() {
    // WiFi and the HTTP server stay running in the background.
    // Only configure(OFF) shuts them down.
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
    stopServer();
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    delay(100);

    switch (mode_) {
        case WifiMgrMode::AP:
            WiFi.mode(WIFI_AP);
            WiFi.softAP(AP_SSID, AP_PASS);
            dns_.start(53, "*", WiFi.softAPIP());
            dnsRunning_ = true;
            startServer();
            break;
        case WifiMgrMode::STA:
            WiFi.mode(WIFI_STA);
            if (ssid_[0]) WiFi.begin(ssid_, pass_[0] ? pass_ : nullptr);
            startServer();
            break;
        case WifiMgrMode::APSTA:
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(AP_SSID, AP_PASS);
            if (ssid_[0]) WiFi.begin(ssid_, pass_[0] ? pass_ : nullptr);
            dns_.start(53, "*", WiFi.softAPIP());
            dnsRunning_ = true;
            startServer();
            break;
        default:
            WiFi.mode(WIFI_OFF);
            break;
    }
}
