#pragma once

#include <U8g2lib.h>

namespace display {

bool begin();

// Get the underlying U8g2 instance for drawing. Always call markDirty()
// after issuing draw commands so flush() actually pushes the buffer.
U8G2& u8g2();

void markDirty();

// Push the framebuffer over I2C only when something has changed (~20ms transfer).
void flush();

}  // namespace display
