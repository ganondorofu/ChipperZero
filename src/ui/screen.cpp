#include "screen.h"

#include "../hal/display.h"
#include "../hal/power.h"
#include "../modules/module_base.h"

namespace screen {

namespace {

constexpr uint8_t kStatusBarH = 12;
constexpr uint8_t kRowH       = 13;
constexpr uint8_t kVisibleRows = 4;  // (64 - 12) / 13 = 4

const char* g_runningTitle = nullptr;

void drawStatusBar() {
    auto& g = display::u8g2();
    g.setFont(u8g2_font_6x10_tf);
    const char* title = g_runningTitle ? g_runningTitle : "ChipperZero";
    g.drawStr(0, 9, title);

    char rhs[8];
    if (power::isCharging()) {
        snprintf(rhs, sizeof(rhs), "CHG");
    } else {
        snprintf(rhs, sizeof(rhs), "%u%%", (unsigned)power::getBatteryPercent());
    }
    int rhsW = g.getStrWidth(rhs);
    g.drawStr(128 - rhsW, 9, rhs);

    g.drawHLine(0, kStatusBarH - 1, 128);
}

}  // namespace

void begin() {
    display::u8g2().clearBuffer();
    drawStatusBar();
    display::markDirty();
}

void drawMenu(const char* title,
              const char* const* labels,
              const bool* enabled,
              uint8_t count,
              uint8_t selectedIdx) {
    g_runningTitle = title;  // show current node title in status bar
    auto& g = display::u8g2();
    g.clearBuffer();
    drawStatusBar();
    g.setFont(u8g2_font_6x10_tf);

    // Compute scroll window so the selected row is visible.
    uint8_t top = 0;
    if (count > kVisibleRows) {
        if (selectedIdx >= kVisibleRows) {
            top = selectedIdx - (kVisibleRows - 1);
        }
        if (top + kVisibleRows > count) top = count - kVisibleRows;
    }

    for (uint8_t row = 0; row < kVisibleRows && (top + row) < count; ++row) {
        uint8_t idx = top + row;
        int y = kStatusBarH + row * kRowH;
        if (idx == selectedIdx) {
            g.drawBox(0, y, 128, kRowH);
            g.setDrawColor(0);
        }
        // Greyed-out (disabled) items get a leading dot prefix; selected still inverts.
        const char* prefix = (enabled && !enabled[idx]) ? "- " : "  ";
        g.drawStr(2, y + 10, prefix);
        g.drawStr(2 + g.getStrWidth("  "), y + 10, labels[idx]);
        if (idx == selectedIdx) {
            g.setDrawColor(1);
        }
    }

    (void)title;  // reserved for breadcrumb in a future revision
    display::markDirty();
}

void drawModuleRunning(IModule* module) {
    g_runningTitle = module ? module->name() : "...";
    auto& g = display::u8g2();
    g.clearBuffer();
    drawStatusBar();
    g.setFont(u8g2_font_6x10_tf);

    char stats[48] = "";
    if (module) module->fillStats(stats, sizeof(stats));
    char* nl = strchr(stats, '\n');
    if (nl) {
        *nl = '\0';
        g.drawStr(0, kStatusBarH + 12, stats[0] ? stats : "Running...");
        g.drawStr(0, kStatusBarH + 25, nl + 1);
        g.drawStr(0, kStatusBarH + 38, "<> scroll  OK:stop");
    } else {
        g.drawStr(0, kStatusBarH + 12, stats[0] ? stats : "Running...");
        g.drawStr(0, kStatusBarH + 25, "<> type  OK:stop");
    }
    display::markDirty();
}

void drawBatteryInfo() {
    g_runningTitle = nullptr;
    auto& g = display::u8g2();
    g.clearBuffer();
    drawStatusBar();
    g.setFont(u8g2_font_6x10_tf);
    char buf[24];
    snprintf(buf, sizeof(buf), "Battery: %u%%", (unsigned)power::getBatteryPercent());
    g.drawStr(0, kStatusBarH + 16, buf);
    g.drawStr(0, kStatusBarH + 32, power::isCharging() ? "Charging" : "On battery");
    g.drawStr(0, kStatusBarH + 48, "Hold OK to back");
    display::markDirty();
}

void drawAbout() {
    g_runningTitle = nullptr;
    auto& g = display::u8g2();
    g.clearBuffer();
    drawStatusBar();
    g.setFont(u8g2_font_6x10_tf);
    g.drawStr(0, kStatusBarH + 16, "ChipperZero v0.1");
    g.drawStr(0, kStatusBarH + 32, "ESP32 + SH1106");
    g.drawStr(0, kStatusBarH + 48, "Hold OK to back");
    display::markDirty();
}

}  // namespace screen
