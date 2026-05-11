#include "menu.h"

#include <Arduino.h>

#include "../hal/ble_remote.h"
#include "../hal/encoder.h"
#include "../modules/ble_spam.h"
#include "../modules/ble_spam_types.h"
#include "../modules/ir.h"
#include "../modules/nfc.h"
#include "../modules/nrf_spam.h"
#include "../modules/nrf_ble_spam.h"
#include "../modules/storage.h"
#include "../modules/ble_scan.h"
#include "../modules/wifi_scan.h"
#include "../modules/wifi_beacon.h"
#include "../modules/wifi_deauth.h"
#include "../modules/wifi_evil_twin.h"
#include "../modules/wifi_sniffer.h"
#include "screen.h"

namespace menu {

namespace {

// ---- Menu model -------------------------------------------------------------

enum class LeafKind : uint8_t {
    NONE,
    MODULE,
    INFO_BATTERY,
    INFO_ABOUT,
    TOGGLE_BLE_REMOTE,
    BACK,
};

struct Node;

struct Item {
    const char* label;
    const Node*  submenu;
    IModule*     module;
    LeafKind     leaf;
};

struct Node {
    const char* title;
    const Item* items;
    uint8_t     count;
};

// ---- Unified spam proxy -----------------------------------------------------
// Selects spam type + radio (ESP32 / NRF24 / Both) before delegating.
// Dual mode disables BLE remote (g_bleSpam.start() calls ble_remote::stop()
// internally; g_bleSpam.stop() restores it).

enum class Radio : uint8_t { ESP = 0, NRF = 1, DUAL = 2 };

static const BleSpamType kSpamTypes[] = {
    BleSpamType::ALL, BleSpamType::APPLE, BleSpamType::GOOGLE,
    BleSpamType::SAMSUNG, BleSpamType::MICROSOFT,
};
static const char* kSpamTypeNames[] = {"All","Apple","Google","Samsung","Windows"};
static constexpr uint8_t kSpamTypeCount = 5;

struct SpamProxy : public IModule {
    BleSpamType type_;
    Radio       radio_;
    const char* name_;

    SpamProxy(BleSpamType t, Radio r, const char* n) : type_(t), radio_(r), name_(n) {}

    bool init()        override { return true; }
    bool isAvailable() override {
        if (radio_ == Radio::ESP)  return g_bleSpam.isAvailable();
        if (radio_ == Radio::NRF)  return g_nrfBleSpam.isAvailable();
        return g_bleSpam.isAvailable() && g_nrfBleSpam.isAvailable();
    }

    void start() override {
        if (radio_ == Radio::ESP  || radio_ == Radio::DUAL) g_bleSpam.setType(type_);
        if (radio_ == Radio::NRF  || radio_ == Radio::DUAL) g_nrfBleSpam.setType(type_);
        if (radio_ == Radio::DUAL) {
            g_nrfBleSpam.start();
            g_bleSpam.start();
        } else if (radio_ == Radio::ESP) {
            g_bleSpam.start();
        } else {
            g_nrfBleSpam.start();
        }
    }

    void stop() override {
        if (radio_ == Radio::DUAL) {
            g_nrfBleSpam.stop();
            g_bleSpam.stop();
        } else if (radio_ == Radio::ESP) {
            g_bleSpam.stop();
        } else {
            g_nrfBleSpam.stop();
        }
    }

    // Rotate spam type with LEFT/RIGHT while running.
    void onEvent(uint8_t ev) override {
        uint8_t idx = 0;
        for (uint8_t i = 0; i < kSpamTypeCount; i++) {
            if (kSpamTypes[i] == type_) { idx = i; break; }
        }
        if (ev == static_cast<uint8_t>(encoder::EVENT_RIGHT))
            idx = (idx + 1) % kSpamTypeCount;
        else if (ev == static_cast<uint8_t>(encoder::EVENT_LEFT))
            idx = (idx + kSpamTypeCount - 1) % kSpamTypeCount;
        else return;
        type_ = kSpamTypes[idx];
        if (radio_ == Radio::ESP  || radio_ == Radio::DUAL) g_bleSpam.setType(type_);
        if (radio_ == Radio::NRF  || radio_ == Radio::DUAL) g_nrfBleSpam.setType(type_);
    }

    void fillStats(char* buf, size_t len) override {
        const char* tname = "?";
        for (uint8_t i = 0; i < kSpamTypeCount; i++) {
            if (kSpamTypes[i] == type_) { tname = kSpamTypeNames[i]; break; }
        }
        char stats[12] = "";
        if (radio_ == Radio::ESP) {
            g_bleSpam.fillStats(stats, sizeof(stats));
        } else if (radio_ == Radio::NRF) {
            g_nrfBleSpam.fillStats(stats, sizeof(stats));
        } else {
            char a[6], b[6];
            g_bleSpam.fillStats(a, sizeof(a));
            g_nrfBleSpam.fillStats(b, sizeof(b));
            snprintf(stats, sizeof(stats), "E:%s N:%s", a, b);
        }
        snprintf(buf, len, "%s | %s", tname, stats);
    }

    const char* name() override { return name_; }
};

// 5 types × 3 radios = 15 proxies (initial type set per menu item)
static SpamProxy s_allEsp    (BleSpamType::ALL,       Radio::ESP,  "ESP32 Spam");
static SpamProxy s_allNrf    (BleSpamType::ALL,       Radio::NRF,  "NRF24 Spam");
static SpamProxy s_allDual   (BleSpamType::ALL,       Radio::DUAL, "Dual Spam" );
static SpamProxy s_appleEsp  (BleSpamType::APPLE,     Radio::ESP,  "ESP32 Spam");
static SpamProxy s_appleNrf  (BleSpamType::APPLE,     Radio::NRF,  "NRF24 Spam");
static SpamProxy s_appleDual (BleSpamType::APPLE,     Radio::DUAL, "Dual Spam" );
static SpamProxy s_googleEsp (BleSpamType::GOOGLE,    Radio::ESP,  "ESP32 Spam");
static SpamProxy s_googleNrf (BleSpamType::GOOGLE,    Radio::NRF,  "NRF24 Spam");
static SpamProxy s_googleDual(BleSpamType::GOOGLE,    Radio::DUAL, "Dual Spam" );
static SpamProxy s_samsEsp   (BleSpamType::SAMSUNG,   Radio::ESP,  "ESP32 Spam");
static SpamProxy s_samsNrf   (BleSpamType::SAMSUNG,   Radio::NRF,  "NRF24 Spam");
static SpamProxy s_samsDual  (BleSpamType::SAMSUNG,   Radio::DUAL, "Dual Spam" );
static SpamProxy s_msEsp     (BleSpamType::MICROSOFT, Radio::ESP,  "ESP32 Spam");
static SpamProxy s_msNrf     (BleSpamType::MICROSOFT, Radio::NRF,  "NRF24 Spam");
static SpamProxy s_msDual    (BleSpamType::MICROSOFT, Radio::DUAL, "Dual Spam" );

// ---- Hardware → Type submenus -----------------------------------------------
#define HW_TYPE_ITEMS(all, apple, google, sams, ms) \
    {"All Devices", nullptr, (all),   LeafKind::MODULE}, \
    {"Apple",       nullptr, (apple), LeafKind::MODULE}, \
    {"Google",      nullptr, (google),LeafKind::MODULE}, \
    {"Samsung",     nullptr, (sams),  LeafKind::MODULE}, \
    {"Windows",     nullptr, (ms),    LeafKind::MODULE}, \
    {"Back",        nullptr, nullptr, LeafKind::BACK  }

const Item kEspItems[]  = { HW_TYPE_ITEMS(&s_allEsp,  &s_appleEsp,  &s_googleEsp,  &s_samsEsp,  &s_msEsp ) };
const Item kNrfItems[]  = { HW_TYPE_ITEMS(&s_allNrf,  &s_appleNrf,  &s_googleNrf,  &s_samsNrf,  &s_msNrf ) };
const Item kDualItems[] = { HW_TYPE_ITEMS(&s_allDual, &s_appleDual, &s_googleDual, &s_samsDual, &s_msDual) };

const Node kEspNode  = {"ESP32 Spam", kEspItems,  sizeof(kEspItems) /sizeof(kEspItems[0]) };
const Node kNrfNode  = {"NRF24 Spam", kNrfItems,  sizeof(kNrfItems) /sizeof(kNrfItems[0]) };
const Node kDualNode = {"Dual Spam",  kDualItems, sizeof(kDualItems)/sizeof(kDualItems[0])};

// ---- BLE Spam submenu -------------------------------------------------------
const Item kBleItems[] = {
    {"ESP32",    &kEspNode,   nullptr,      LeafKind::NONE  },
    {"NRF24",    &kNrfNode,   nullptr,      LeafKind::NONE  },
    {"Dual",     &kDualNode,  nullptr,      LeafKind::NONE  },
    {"NRF Spam", nullptr,     &g_nrfSpam,   LeafKind::MODULE},
    {"Scanner",  nullptr,     &g_bleScan,   LeafKind::MODULE},
    {"Back",     nullptr,     nullptr,      LeafKind::BACK  },
};
const Node kBleNode = {"BLE Spam", kBleItems, sizeof(kBleItems)/sizeof(kBleItems[0])};

// ---- System submenu ---------------------------------------------------------
const Item kSysItems[] = {
    {"Battery",    nullptr, nullptr, LeafKind::INFO_BATTERY   },
    {"BLE Remote", nullptr, nullptr, LeafKind::TOGGLE_BLE_REMOTE},
    {"About",      nullptr, nullptr, LeafKind::INFO_ABOUT     },
    {"Back",       nullptr, nullptr, LeafKind::BACK           },
};
const Node kSysNode = {"System", kSysItems, sizeof(kSysItems)/sizeof(kSysItems[0])};

// ---- IR proxy ---------------------------------------------------------------

struct IrProxy : public IModule {
    IrMode      mode_;
    const char* name_;
    IrProxy(IrMode m, const char* n) : mode_(m), name_(n) {}
    bool init()        override { return false; }
    bool isAvailable() override { return g_ir.isAvailable(); }
    void start()       override { g_ir.setMode(mode_, name_); g_ir.start(); }
    void stop()        override { g_ir.stop(); }
    void onEvent(uint8_t ev)    override { g_ir.onEvent(ev); }
    void fillStats(char* b, size_t l) override { g_ir.fillStats(b, l); }
    const char* name() override { return name_; }
};

static IrProxy s_irTvKill (IrMode::TV_KILL, "TV Kill");
static IrProxy s_irCapture(IrMode::CAPTURE, "IR Capture");
static IrProxy s_irReplay (IrMode::REPLAY,  "IR Replay");

// ---- NFC proxy --------------------------------------------------------------

struct NfcProxy : public IModule {
    NfcMode     mode_;
    const char* name_;
    NfcProxy(NfcMode m, const char* n) : mode_(m), name_(n) {}
    bool init()        override { return false; }
    bool isAvailable() override { return g_nfc.isAvailable(); }
    void start()       override { g_nfc.setMode(mode_, name_); g_nfc.start(); }
    void stop()        override { g_nfc.stop(); }
    void onEvent(uint8_t ev)    override { g_nfc.onEvent(ev); }
    void fillStats(char* b, size_t l) override { g_nfc.fillStats(b, l); }
    const char* name() override { return name_; }
};

static NfcProxy s_nfcRead   (NfcMode::READ,    "NFC Read");
static NfcProxy s_nfcWrite  (NfcMode::WRITE,   "NFC Clone");
static NfcProxy s_nfcEmulate(NfcMode::EMULATE, "NFC Emulate");

// ---- IR submenu -------------------------------------------------------------

const Item kIrItems[] = {
    {"TV Kill",  nullptr, &s_irTvKill,  LeafKind::MODULE},
    {"Capture",  nullptr, &s_irCapture, LeafKind::MODULE},
    {"Replay",   nullptr, &s_irReplay,  LeafKind::MODULE},
    {"Back",     nullptr, nullptr,      LeafKind::BACK  },
};
const Node kIrNode = {"IR", kIrItems, sizeof(kIrItems)/sizeof(kIrItems[0])};

// ---- NFC submenu ------------------------------------------------------------

const Item kNfcItems[] = {
    {"Read/Save", nullptr, &s_nfcRead,    LeafKind::MODULE},
    {"Clone",     nullptr, &s_nfcWrite,   LeafKind::MODULE},
    {"Emulate",   nullptr, &s_nfcEmulate, LeafKind::MODULE},
    {"Back",      nullptr, nullptr,       LeafKind::BACK  },
};
const Node kNfcNode = {"NFC", kNfcItems, sizeof(kNfcItems)/sizeof(kNfcItems[0])};

// ---- WiFi submenu -----------------------------------------------------------
const Item kWifiItems[] = {
    {"AP Scan",     nullptr, &g_wifiScan,      LeafKind::MODULE},
    {"Beacon Spam", nullptr, &g_wifiBeacon,    LeafKind::MODULE},
    {"Deauth",      nullptr, &g_wifiDeauth,    LeafKind::MODULE},
    {"Evil Twin",   nullptr, &g_wifiEvilTwin,  LeafKind::MODULE},
    {"Sniffer",     nullptr, &g_wifiSniffer,   LeafKind::MODULE},
    {"Back",        nullptr, nullptr,          LeafKind::BACK  },
};
const Node kWifiNode = {"WiFi", kWifiItems, sizeof(kWifiItems)/sizeof(kWifiItems[0])};

// ---- Root menu --------------------------------------------------------------
const Item kRootItems[] = {
    {"BLE Spam", &kBleNode,  nullptr,    LeafKind::NONE  },
    {"NFC",      &kNfcNode,  nullptr,    LeafKind::NONE  },
    {"IR",       &kIrNode,   nullptr,    LeafKind::NONE  },
    {"WiFi",     &kWifiNode, nullptr,    LeafKind::NONE  },
    {"Storage",  nullptr,    &g_storage, LeafKind::MODULE},
    {"System",   &kSysNode,  nullptr,    LeafKind::NONE  },
};
const Node kRootNode = {"ChipperZero", kRootItems, sizeof(kRootItems)/sizeof(kRootItems[0])};

// ---- View state -------------------------------------------------------------

enum class View : uint8_t {
    LIST,
    MODULE_RUNNING,
    INFO_BATTERY,
    INFO_ABOUT,
};

constexpr uint8_t kStackMax = 4;
const Node* g_stack[kStackMax];
uint8_t     g_stackDepth = 0;
uint8_t     g_selected[kStackMax] = {0};

View      g_view = View::LIST;
IModule*  g_active = nullptr;
bool      g_dirty = true;

const Node* curNode() { return g_stack[g_stackDepth - 1]; }
uint8_t&    curSel()  { return g_selected[g_stackDepth - 1]; }

void pushNode(const Node* n) {
    if (g_stackDepth >= kStackMax) return;
    g_stack[g_stackDepth] = n;
    g_selected[g_stackDepth] = 0;
    g_stackDepth++;
}

void popNode() {
    if (g_stackDepth > 1) g_stackDepth--;
}

bool itemEnabled(const Item& it) {
    if (it.leaf == LeafKind::MODULE && it.module) return it.module->isAvailable();
    return true;
}

void render() {
    if (g_view == View::MODULE_RUNNING) {
        screen::drawModuleRunning(g_active);
        return;
    }
    if (g_view == View::INFO_BATTERY) {
        screen::drawBatteryInfo();
        return;
    }
    if (g_view == View::INFO_ABOUT) {
        screen::drawAbout();
        return;
    }
    const Node* n = curNode();
    static const char* labels[16];
    static char bleLabelBuf[24];
    static bool enabled[16];
    uint8_t count = n->count <= 16 ? n->count : 16;
    for (uint8_t i = 0; i < count; ++i) {
        if (n->items[i].leaf == LeafKind::TOGGLE_BLE_REMOTE) {
            snprintf(bleLabelBuf, sizeof(bleLabelBuf), "BLE Remote: %s",
                     ble_remote::isEnabled() ? "ON" : "OFF");
            labels[i] = bleLabelBuf;
        } else {
            labels[i] = n->items[i].label;
        }
        enabled[i] = itemEnabled(n->items[i]);
    }
    screen::drawMenu(n->title, labels, enabled, count, curSel());
}

void launchModule(IModule* m) {
    if (g_active != nullptr) return;
    if (!m || !m->isAvailable()) return;
    g_active = m;
    g_active->start();
    g_view = View::MODULE_RUNNING;
    g_dirty = true;
}

void stopActiveModule() {
    if (!g_active) return;
    g_active->stop();
    g_active = nullptr;
    g_view = View::LIST;
    g_dirty = true;
}

void activateItem(const Item& it) {
    if (it.submenu) {
        pushNode(it.submenu);
        g_dirty = true;
        return;
    }
    switch (it.leaf) {
        case LeafKind::MODULE:        launchModule(it.module); break;
        case LeafKind::INFO_BATTERY:  g_view = View::INFO_BATTERY; g_dirty = true; break;
        case LeafKind::INFO_ABOUT:    g_view = View::INFO_ABOUT;   g_dirty = true; break;
        case LeafKind::TOGGLE_BLE_REMOTE:
            ble_remote::setEnabled(!ble_remote::isEnabled());
            g_dirty = true;
            break;
        case LeafKind::BACK: if (g_stackDepth > 1) { popNode(); g_dirty = true; } break;
        case LeafKind::NONE: break;
    }
}

void handleEvent(encoder::InputEvent ev) {
    if (ev == encoder::EVENT_NONE) return;

    if (g_view == View::MODULE_RUNNING) {
        if (ev == encoder::EVENT_BACK) {
            stopActiveModule();
        } else if (g_active && (ev == encoder::EVENT_LEFT ||
                                ev == encoder::EVENT_RIGHT ||
                                ev == encoder::EVENT_OK)) {
            g_active->onEvent(static_cast<uint8_t>(ev));
            g_dirty = true;
        }
        return;
    }
    if (g_view == View::INFO_BATTERY || g_view == View::INFO_ABOUT) {
        if (ev == encoder::EVENT_BACK || ev == encoder::EVENT_OK) {
            g_view = View::LIST;
            g_dirty = true;
        }
        return;
    }

    const Node* n = curNode();
    uint8_t& sel = curSel();
    switch (ev) {
        case encoder::EVENT_LEFT:
            sel = (sel > 0) ? sel - 1 : n->count - 1;
            g_dirty = true;
            break;
        case encoder::EVENT_RIGHT:
            sel = (sel + 1 < n->count) ? sel + 1 : 0;
            g_dirty = true;
            break;
        case encoder::EVENT_OK:
            activateItem(n->items[sel]);
            break;
        case encoder::EVENT_BACK:
            if (g_stackDepth > 1) { popNode(); g_dirty = true; }
            break;
        default:
            break;
    }
}

}  // namespace

void begin() {
    g_stackDepth = 0;
    pushNode(&kRootNode);
    g_view = View::LIST;
    g_active = nullptr;
    g_dirty = true;
    screen::begin();
}

void update() {
    encoder::InputEvent ev = encoder::tick();
    handleEvent(ev);

    // Refresh running screen every second to update stats.
    static uint32_t lastStatRefresh = 0;
    if (g_view == View::MODULE_RUNNING) {
        uint32_t now = millis();
        if (now - lastStatRefresh >= 1000) {
            lastStatRefresh = now;
            g_dirty = true;
        }
    }

    if (g_dirty) {
        render();
        g_dirty = false;
    }
}

IModule* activeModule() { return g_active; }

}  // namespace menu
