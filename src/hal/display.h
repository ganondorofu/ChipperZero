#pragma once

#include <U8g2lib.h>

namespace display {

bool begin();

// Wire.end()+Wire.begin()+initDisplay() — call after any module that resets Wire.
void reinit();

// Get the underlying U8g2 instance for drawing. Always call markDirty()
// after issuing draw commands so flush() actually pushes the buffer.
U8G2& u8g2();

void markDirty();

// Push the framebuffer over I2C only when something has changed (~20ms transfer).
void flush();

// Turn the OLED panel on (false) or off (true). Does not affect the buffer.
void setSleep(bool on);

}  // namespace display
