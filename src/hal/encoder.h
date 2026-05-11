#pragma once

namespace encoder {

enum InputEvent {
    EVENT_NONE = 0,
    EVENT_LEFT,   // rotary CCW
    EVENT_RIGHT,  // rotary CW
    EVENT_OK,     // encoder push (short)
    EVENT_BACK,   // encoder push (long, ~600 ms)
};

void begin();

// Poll the rotary encoder and the three buttons. Call from loop().
// Returns at most one event per call; remaining events are returned on subsequent calls.
// Hardware input has priority; queued events from injectEvent() are returned when
// no hardware event is pending.
InputEvent tick();

// Inject an event from another source (e.g. BLE remote, serial console).
// Thread-safe: backed by a FreeRTOS queue and safe to call from any task.
// Returns false if the queue is full (event dropped).
bool injectEvent(InputEvent ev);

}  // namespace encoder
