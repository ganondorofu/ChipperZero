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
#include "../modules/wifi_beacon_clone.h"
#include "../modules/wifi_sniffer.h"
#include "../modules/wifi_evil_portal.h"
#include "../modules/wifi_spectrum.h"
#include "../modules/nrf_jammer.h"
#include "../modules/nrf_mousejack.h"
#include "screen.h"
#include "../hal/keyboard.h"

namespace menu {

namespace {

// ---- Menu model -------------------------------------------------------------

enum class LeafKind : uint8_t {
    NONE,
    MODULE,
    INFO_BATTERY,
    INFO_ABOUT,
    TOGGLE_BLE_REMOTE,
    OPEN_KEYBOARD,
    OPEN_KB_BEACON,
    OPEN_KB_MJ,
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

static bool nrfSupports(BleSpamType t) {
    switch (t) {
        case BleSpamType::ALL:
        case BleSpamType::APPLE:
        case BleSpamType::GOOGLE:
        case BleSpamType::SAMSUNG:
        case BleSpamType::MICROSOFT:
            return true;
        default:
            return false;
    }
}

struct SpamProxy : public IModule {
    BleSpamType type_;
    Radio       radio_;
    const char* name_;

    SpamProxy(BleSpamType t, Radio r, const char* n) : type_(t), radio_(r), name_(n) {}

    bool init()        override { return true; }
    bool isAvailable() override {
        if (radio_ == Radio::NRF)  return g_nrfBleSpam.isAvailable() && nrfSupports(type_);
        if (radio_ == Radio::DUAL) return g_bleSpam.isAvailable() && g_nrfBleSpam.isAvailable();
        return g_bleSpam.isAvailable();
    }

    void start() override {
        if (radio_ == Radio::ESP  || radio_ == Radio::DUAL) g_bleSpam.setType(type_);
        if (radio_ == Radio::NRF  || radio_ == Radio::DUAL) g_nrfBleSpam.setType(type_);
        if (radio_ == Radio::DUAL) {
            if (nrfSupports(type_)) g_nrfBleSpam.start();
            g_bleSpam.start();
        } else if (radio_ == Radio::ESP) {
            g_bleSpam.start();
        } else {
            g_nrfBleSpam.start();
        }
    }

    void stop() override {
        if (radio_ == Radio::DUAL) {
            if (nrfSupports(type_)) g_nrfBleSpam.stop();
            g_bleSpam.stop();
        } else if (radio_ == Radio::ESP) {
            g_bleSpam.stop();
        } else {
            g_nrfBleSpam.stop();
        }
    }

    void onEvent(uint8_t) override {}

    void fillStats(char* buf, size_t len) override {
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
        snprintf(buf, len, "%s", stats);
    }

    const char* name() override { return name_; }
};

// ---- SpamProxy instances: type × radio ---------------------------------------
// All
static SpamProxy s_allEsp    (BleSpamType::ALL,           Radio::ESP,  "All Spam");
static SpamProxy s_allNrf    (BleSpamType::ALL,           Radio::NRF,  "All Spam");
static SpamProxy s_allDual   (BleSpamType::ALL,           Radio::DUAL, "All Spam");
// Apple
static SpamProxy s_appleEsp    (BleSpamType::APPLE,           Radio::ESP,  "Apple All");
static SpamProxy s_appleNrf    (BleSpamType::APPLE,           Radio::NRF,  "Apple All");
static SpamProxy s_appleDual   (BleSpamType::APPLE,           Radio::DUAL, "Apple All");
static SpamProxy s_apActEsp    (BleSpamType::APPLE_ACTION,    Radio::ESP,  "Action");
static SpamProxy s_apActNrf    (BleSpamType::APPLE_ACTION,    Radio::NRF,  "Action");
static SpamProxy s_apActDual   (BleSpamType::APPLE_ACTION,    Radio::DUAL, "Action");
static SpamProxy s_apPodsEsp   (BleSpamType::APPLE_AIRPODS,   Radio::ESP,  "AirPods");
static SpamProxy s_apPodsNrf   (BleSpamType::APPLE_AIRPODS,   Radio::NRF,  "AirPods");
static SpamProxy s_apPodsDual  (BleSpamType::APPLE_AIRPODS,   Radio::DUAL, "AirPods");
static SpamProxy s_apTagEsp    (BleSpamType::APPLE_AIRTAG,    Radio::ESP,  "AirTag");
static SpamProxy s_apTagNrf    (BleSpamType::APPLE_AIRTAG,    Radio::NRF,  "AirTag");
static SpamProxy s_apTagDual   (BleSpamType::APPLE_AIRTAG,    Radio::DUAL, "AirTag");
static SpamProxy s_apNearEsp   (BleSpamType::APPLE_NEARBY,    Radio::ESP,  "Nearby");
static SpamProxy s_apNearNrf   (BleSpamType::APPLE_NEARBY,    Radio::NRF,  "Nearby");
static SpamProxy s_apNearDual  (BleSpamType::APPLE_NEARBY,    Radio::DUAL, "Nearby");
// Google
static SpamProxy s_googleEsp (BleSpamType::GOOGLE,    Radio::ESP,  "Google");
static SpamProxy s_googleNrf (BleSpamType::GOOGLE,    Radio::NRF,  "Google");
static SpamProxy s_googleDual(BleSpamType::GOOGLE,    Radio::DUAL, "Google");
// Samsung
static SpamProxy s_samsEsp    (BleSpamType::SAMSUNG,       Radio::ESP,  "Samsung All");
static SpamProxy s_samsNrf    (BleSpamType::SAMSUNG,       Radio::NRF,  "Samsung All");
static SpamProxy s_samsDual   (BleSpamType::SAMSUNG,       Radio::DUAL, "Samsung All");
static SpamProxy s_samsWEsp   (BleSpamType::SAMSUNG_WATCH, Radio::ESP,  "Watch");
static SpamProxy s_samsWNrf   (BleSpamType::SAMSUNG_WATCH, Radio::NRF,  "Watch");
static SpamProxy s_samsWDual  (BleSpamType::SAMSUNG_WATCH, Radio::DUAL, "Watch");
static SpamProxy s_samsBEsp   (BleSpamType::SAMSUNG_BUDS,  Radio::ESP,  "Buds");
static SpamProxy s_samsBNrf   (BleSpamType::SAMSUNG_BUDS,  Radio::NRF,  "Buds");
static SpamProxy s_samsBDual  (BleSpamType::SAMSUNG_BUDS,  Radio::DUAL, "Buds");
// Microsoft
static SpamProxy s_msEsp     (BleSpamType::MICROSOFT, Radio::ESP,  "Windows");
static SpamProxy s_msNrf     (BleSpamType::MICROSOFT, Radio::NRF,  "Windows");
static SpamProxy s_msDual    (BleSpamType::MICROSOFT, Radio::DUAL, "Windows");

// ---- Apple sub-submenus (ESP / NRF / Dual) ----------------------------------
#define APPLE_ITEMS(all, act, pods, tag, near) \
    {"All Apple",  nullptr, (all),  LeafKind::MODULE}, \
    {"AirPods",    nullptr, (pods), LeafKind::MODULE}, \
    {"AirTag",     nullptr, (tag),  LeafKind::MODULE}, \
    {"Action",     nullptr, (act),  LeafKind::MODULE}, \
    {"Nearby",     nullptr, (near), LeafKind::MODULE}, \
    {"Back",       nullptr, nullptr,LeafKind::BACK  }

const Item kAppleEspItems[]  = { APPLE_ITEMS(&s_appleEsp,  &s_apActEsp,  &s_apPodsEsp,  &s_apTagEsp,  &s_apNearEsp ) };
const Item kAppleNrfItems[]  = { APPLE_ITEMS(&s_appleNrf,  &s_apActNrf,  &s_apPodsNrf,  &s_apTagNrf,  &s_apNearNrf ) };
const Item kAppleDualItems[] = { APPLE_ITEMS(&s_appleDual, &s_apActDual, &s_apPodsDual, &s_apTagDual, &s_apNearDual) };

const Node kAppleEspNode  = {"Apple (ESP)",  kAppleEspItems,  sizeof(kAppleEspItems) /sizeof(kAppleEspItems[0]) };
const Node kAppleNrfNode  = {"Apple (NRF)",  kAppleNrfItems,  sizeof(kAppleNrfItems) /sizeof(kAppleNrfItems[0]) };
const Node kAppleDualNode = {"Apple (Dual)", kAppleDualItems, sizeof(kAppleDualItems)/sizeof(kAppleDualItems[0])};

// ---- Samsung sub-submenus ---------------------------------------------------
#define SAMSUNG_ITEMS(all, watch, buds) \
    {"All Samsung", nullptr, (all),   LeafKind::MODULE}, \
    {"Watch",       nullptr, (watch), LeafKind::MODULE}, \
    {"Buds",        nullptr, (buds),  LeafKind::MODULE}, \
    {"Back",        nullptr, nullptr, LeafKind::BACK  }

const Item kSamsEspItems[]  = { SAMSUNG_ITEMS(&s_samsEsp,  &s_samsWEsp,  &s_samsBEsp ) };
const Item kSamsNrfItems[]  = { SAMSUNG_ITEMS(&s_samsNrf,  &s_samsWNrf,  &s_samsBNrf ) };
const Item kSamsDualItems[] = { SAMSUNG_ITEMS(&s_samsDual, &s_samsWDual, &s_samsBDual) };

const Node kSamsEspNode  = {"Samsung (ESP)",  kSamsEspItems,  sizeof(kSamsEspItems) /sizeof(kSamsEspItems[0]) };
const Node kSamsNrfNode  = {"Samsung (NRF)",  kSamsNrfItems,  sizeof(kSamsNrfItems) /sizeof(kSamsNrfItems[0]) };
const Node kSamsDualNode = {"Samsung (Dual)", kSamsDualItems, sizeof(kSamsDualItems)/sizeof(kSamsDualItems[0])};

// ---- Hardware → Type submenus -----------------------------------------------
#define HW_TYPE_ITEMS(appleNode, all, google, ms, samsNode) \
    {"All Devices", nullptr,    (all),    LeafKind::MODULE}, \
    {"Apple",       (appleNode),nullptr,  LeafKind::NONE  }, \
    {"Google",      nullptr,    (google), LeafKind::MODULE}, \
    {"Samsung",     (samsNode), nullptr,  LeafKind::NONE  }, \
    {"Windows",     nullptr,    (ms),     LeafKind::MODULE}, \
    {"Back",        nullptr,    nullptr,  LeafKind::BACK  }

const Item kEspItems[]  = { HW_TYPE_ITEMS(&kAppleEspNode,  &s_allEsp,  &s_googleEsp,  &s_msEsp,  &kSamsEspNode ) };
const Item kNrfItems[]  = { HW_TYPE_ITEMS(&kAppleNrfNode,  &s_allNrf,  &s_googleNrf,  &s_msNrf,  &kSamsNrfNode ) };
const Item kDualItems[] = { HW_TYPE_ITEMS(&kAppleDualNode, &s_allDual, &s_googleDual, &s_msDual, &kSamsDualNode) };

const Node kEspNode  = {"ESP32 Spam", kEspItems,  sizeof(kEspItems) /sizeof(kEspItems[0]) };
const Node kNrfNode  = {"NRF24 Spam", kNrfItems,  sizeof(kNrfItems) /sizeof(kNrfItems[0]) };
const Node kDualNode = {"Dual Spam",  kDualItems, sizeof(kDualItems)/sizeof(kDualItems[0])};

// ---- BLE Spam submenu -------------------------------------------------------
const Item kBleItems[] = {
    {"ESP32",      &kEspNode,   nullptr,          LeafKind::NONE     },
    {"NRF24",      &kNrfNode,   nullptr,          LeafKind::NONE     },
    {"Dual",       &kDualNode,  nullptr,          LeafKind::NONE     },
    {"NRF Spam",   nullptr,     &g_nrfSpam,       LeafKind::MODULE   },
    {"MouseJack",  nullptr,     &g_nrfMousejack,  LeafKind::MODULE   },
    {"MJ Custom",  nullptr,     nullptr,          LeafKind::OPEN_KB_MJ},
    {"Scanner",    nullptr,     &g_bleScan,       LeafKind::MODULE   },
    {"BT Jammer",  nullptr,     &g_nrfJammer,     LeafKind::MODULE   },
    {"Back",       nullptr,     nullptr,          LeafKind::BACK     },
};
const Node kBleNode = {"BLE Spam", kBleItems, sizeof(kBleItems)/sizeof(kBleItems[0])};

// ---- System submenu ---------------------------------------------------------
const Item kSysItems[] = {
    {"Battery",    nullptr, nullptr, LeafKind::INFO_BATTERY      },
    {"BLE Remote", nullptr, nullptr, LeafKind::TOGGLE_BLE_REMOTE },
    {"Keyboard",   nullptr, nullptr, LeafKind::OPEN_KEYBOARD     },
    {"About",      nullptr, nullptr, LeafKind::INFO_ABOUT        },
    {"Back",       nullptr, nullptr, LeafKind::BACK              },
};
const Node kSysNode = {"System", kSysItems, sizeof(kSysItems)/sizeof(kSysItems[0])};

// ---- IR proxy ---------------------------------------------------------------

struct IrProxy : public IModule {
    IrMode      mode_;
    const char* name_;
    IrProxy(IrMode m, const char* n) : mode_(m), name_(n) {}
    bool init()        override { return true; }
    bool isAvailable() override { return g_ir.isAvailable(); }
    void start()       override { g_ir.setMode(mode_, name_); g_ir.start(); }
    void stop()        override { g_ir.stop(); }
    void onEvent(uint8_t ev)    override { g_ir.onEvent(ev); }
    void fillStats(char* b, size_t l) override { g_ir.fillStats(b, l); }
    const char* name() override { return name_; }
};

struct IrPresetProxy : public IModule {
    uint8_t     cat_;
    const char* name_;
    IrPresetProxy(uint8_t cat, const char* n) : cat_(cat), name_(n) {}
    bool init()        override { return true; }
    bool isAvailable() override { return g_ir.isAvailable(); }
    void start()       override {
        g_ir.setMode(IrMode::PRESET, name_);
        g_ir.setPresetCat(cat_);
        g_ir.start();
    }
    void stop()        override { g_ir.stop(); }
    void onEvent(uint8_t ev)    override { g_ir.onEvent(ev); }
    void fillStats(char* b, size_t l) override { g_ir.fillStats(b, l); }
    const char* name() override { return name_; }
};

static IrProxy       s_irTvKill (IrMode::TV_KILL, "TV Kill");
static IrProxy       s_irCapture(IrMode::CAPTURE,  "IR Record");
static IrProxy       s_irReplay (IrMode::REPLAY,   "IR Replay");
static IrPresetProxy s_irPrPwr  (0, "TV Power");
static IrPresetProxy s_irPrVolUp(1, "TV Vol+");
static IrPresetProxy s_irPrVolDn(2, "TV Vol-");
static IrPresetProxy s_irPrMute (3, "TV Mute");

const Item kIrPresetItems[] = {
    {"TV Power", nullptr, &s_irPrPwr,   LeafKind::MODULE},
    {"TV Vol+",  nullptr, &s_irPrVolUp, LeafKind::MODULE},
    {"TV Vol-",  nullptr, &s_irPrVolDn, LeafKind::MODULE},
    {"TV Mute",  nullptr, &s_irPrMute,  LeafKind::MODULE},
    {"Back",     nullptr, nullptr,      LeafKind::BACK  },
};
const Node kIrPresetNode = {"Presets", kIrPresetItems, sizeof(kIrPresetItems)/sizeof(kIrPresetItems[0])};

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
    {"TV Kill",  nullptr,        &s_irTvKill,  LeafKind::MODULE},
    {"Presets",  &kIrPresetNode, nullptr,      LeafKind::NONE  },
    {"Record",   nullptr,        &s_irCapture, LeafKind::MODULE},
    {"Replay",   nullptr,        &s_irReplay,  LeafKind::MODULE},
    {"Back",     nullptr,        nullptr,      LeafKind::BACK  },
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

// ---- Beacon proxy (count preset) -------------------------------------------
struct BeaconProxy : public IModule {
    uint16_t    count_;
    const char* name_;
    BeaconProxy(uint16_t c, const char* n) : count_(c), name_(n) {}
    bool init()        override { return true; }
    bool isAvailable() override { return g_wifiBeacon.isAvailable(); }
    void start()       override { g_wifiBeacon.setCount(count_); g_wifiBeacon.start(); }
    void stop()        override { g_wifiBeacon.stop(); }
    void onEvent(uint8_t ev)    override { g_wifiBeacon.onEvent(ev); }
    void fillStats(char* b, size_t l) override { g_wifiBeacon.fillStats(b, l); }
    const char* name() override { return name_; }
};

static BeaconProxy s_beaconUnlim(0,   "Beacon ∞");
static BeaconProxy s_beacon10   (10,  "Beacon x10");
static BeaconProxy s_beacon50   (50,  "Beacon x50");
static BeaconProxy s_beacon100  (100, "Beacon x100");

// ---- Beacon submenu ---------------------------------------------------------
const Item kBeaconItems[] = {
    {"Unlimited",   nullptr, &s_beaconUnlim,     LeafKind::MODULE      },
    {"Burst 10",    nullptr, &s_beacon10,        LeafKind::MODULE      },
    {"Burst 50",    nullptr, &s_beacon50,        LeafKind::MODULE      },
    {"Burst 100",   nullptr, &s_beacon100,       LeafKind::MODULE      },
    {"Clone",       nullptr, &g_wifiBeaconClone, LeafKind::MODULE      },
    {"Custom SSID", nullptr, nullptr,            LeafKind::OPEN_KB_BEACON},
    {"Back",        nullptr, nullptr,            LeafKind::BACK        },
};
const Node kBeaconNode = {"Beacon Spam", kBeaconItems, sizeof(kBeaconItems)/sizeof(kBeaconItems[0])};

// ---- Deauth proxy -----------------------------------------------------------
struct DeauthProxy : public IModule {
    DeauthMode  mode_;
    const char* name_;
    DeauthProxy(DeauthMode m, const char* n) : mode_(m), name_(n) {}
    bool init()        override { return true; }
    bool isAvailable() override { return g_wifiDeauth.isAvailable(); }
    void start()       override { g_wifiDeauth.setMode(mode_); g_wifiDeauth.start(); }
    void stop()        override { g_wifiDeauth.stop(); }
    void onEvent(uint8_t ev)   override { g_wifiDeauth.onEvent(ev); }
    void fillStats(char* b, size_t l) override { g_wifiDeauth.fillStats(b, l); }
    const char* name() override { return name_; }
};

static DeauthProxy s_deauthRandom  (DeauthMode::RANDOM,   "Deauth All");
static DeauthProxy s_deauthTargeted(DeauthMode::TARGETED, "Deauth Target");

const Item kDeauthItems[] = {
    {"Indiscriminate", nullptr, &s_deauthRandom,   LeafKind::MODULE},
    {"Targeted",       nullptr, &s_deauthTargeted, LeafKind::MODULE},
    {"Back",           nullptr, nullptr,           LeafKind::BACK  },
};
const Node kDeauthNode = {"Deauth", kDeauthItems, sizeof(kDeauthItems)/sizeof(kDeauthItems[0])};

// ---- Evil Twin proxy --------------------------------------------------------
struct EvilTwinProxy : public IModule {
    bool        auto_;
    const char* name_;
    EvilTwinProxy(bool a, const char* n) : auto_(a), name_(n) {}
    bool init()        override { return true; }
    bool isAvailable() override { return g_wifiEvilTwin.isAvailable(); }
    void start()       override { g_wifiEvilTwin.setAuto(auto_); g_wifiEvilTwin.start(); }
    void stop()        override { g_wifiEvilTwin.stop(); }
    void onEvent(uint8_t ev)   override { g_wifiEvilTwin.onEvent(ev); }
    void fillStats(char* b, size_t l) override { g_wifiEvilTwin.fillStats(b, l); }
    const char* name() override { return name_; }
};

static EvilTwinProxy s_twinAuto  (true,  "Twin Auto");
static EvilTwinProxy s_twinManual(false, "Twin Manual");

const Item kEvilTwinItems[] = {
    {"Auto (strongest)", nullptr, &s_twinAuto,   LeafKind::MODULE},
    {"Manual",           nullptr, &s_twinManual, LeafKind::MODULE},
    {"Back",             nullptr, nullptr,       LeafKind::BACK  },
};
const Node kEvilTwinNode = {"Evil Twin", kEvilTwinItems, sizeof(kEvilTwinItems)/sizeof(kEvilTwinItems[0])};

// ---- WiFi submenu -----------------------------------------------------------
const Item kWifiItems[] = {
    {"AP Scan",     nullptr,        &g_wifiScan,        LeafKind::MODULE},
    {"Spectrum",    nullptr,        &g_wifiSpectrum,    LeafKind::MODULE},
    {"Beacon Spam", &kBeaconNode,   nullptr,            LeafKind::NONE  },
    {"Deauth",      &kDeauthNode,   nullptr,            LeafKind::NONE  },
    {"Evil Twin",   &kEvilTwinNode, nullptr,            LeafKind::NONE  },
    {"Evil Portal", nullptr,        &g_wifiEvilPortal,  LeafKind::MODULE},
    {"Sniffer",     nullptr,        &g_wifiSniffer,     LeafKind::MODULE},
    {"Back",        nullptr,        nullptr,            LeafKind::BACK  },
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
    KEYBOARD,
};

constexpr uint8_t kStackMax = 6;
const Node* g_stack[kStackMax];
uint8_t     g_stackDepth = 0;
uint8_t     g_selected[kStackMax] = {0};

enum class KbAction : uint8_t { NONE, BEACON_CUSTOM, MJ_CUSTOM };

View             g_view     = View::LIST;
IModule*         g_active   = nullptr;
bool             g_dirty    = true;
keyboard::State  g_kbState;
KbAction         g_kbAction = KbAction::NONE;

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
        if (g_active && g_active->hasCustomDraw()) {
            g_active->draw();
        } else {
            screen::drawModuleRunning(g_active);
        }
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
    if (g_view == View::KEYBOARD) {
        keyboard::render(g_kbState);
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
        case LeafKind::OPEN_KEYBOARD:
            keyboard::init(g_kbState);
            g_kbAction = KbAction::NONE;
            g_view = View::KEYBOARD;
            g_dirty = true;
            break;
        case LeafKind::OPEN_KB_BEACON:
            keyboard::init(g_kbState);
            g_kbAction = KbAction::BEACON_CUSTOM;
            g_view = View::KEYBOARD;
            g_dirty = true;
            break;
        case LeafKind::OPEN_KB_MJ:
            keyboard::init(g_kbState);
            g_kbAction = KbAction::MJ_CUSTOM;
            g_view = View::KEYBOARD;
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
    if (g_view == View::KEYBOARD) {
        keyboard::handleEvent(g_kbState, static_cast<uint8_t>(ev));
        if (g_kbState.done) {
            if (g_kbState.ok && g_kbState.len > 0) {
                switch (g_kbAction) {
                    case KbAction::BEACON_CUSTOM:
                        g_wifiBeacon.setCustomSSID(g_kbState.buf);
                        g_wifiBeacon.setCount(0);
                        launchModule(&g_wifiBeacon);
                        break;
                    case KbAction::MJ_CUSTOM:
                        g_nrfMousejack.setCustomText(g_kbState.buf);
                        g_nrfMousejack.payloadIdx_ = 3;  // select Custom slot
                        launchModule(&g_nrfMousejack);
                        break;
                    default:
                        g_view = View::LIST;
                        break;
                }
            } else {
                g_view = View::LIST;
            }
            g_kbAction = KbAction::NONE;
        }
        g_dirty = true;
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
