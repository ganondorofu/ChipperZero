#pragma once

#include "../modules/module_base.h"

namespace menu {

void begin();

// Drive one frame of UI: consume an InputEvent (may be EVENT_NONE) and redraw if needed.
void update();

// Currently running module, or nullptr. The status bar uses this for its title.
IModule* activeModule();

// Request launch/stop from outside the UI loop (e.g. WiFi HTTP handlers).
// Safe to call from the same task as loop(); processed at the top of the next update().
void requestLaunch(IModule* m);
void requestStop();

}  // namespace menu
