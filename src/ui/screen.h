#pragma once

#include <stdint.h>

class IModule;

namespace screen {

void begin();

// Render the menu list with the given title and selection.
// items[]/count describe the visible list. selectedIdx is the highlighted row.
void drawMenu(const char* title,
              const char* const* labels,
              const bool* enabled,
              uint8_t count,
              uint8_t selectedIdx);

// Render the screen shown while a module is active (BACK to return).
void drawModuleRunning(IModule* module);

void drawBatteryInfo();
void drawAbout();

}  // namespace screen
