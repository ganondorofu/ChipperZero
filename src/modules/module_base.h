#pragma once

#include <stddef.h>   // size_t
#include <stdint.h>   // uint8_t

class IModule {
public:
    virtual ~IModule() = default;

    // One-shot setup. Return false to mark the module as greyed-out in the menu.
    virtual bool init() = 0;

    // Whether the module can currently be launched (hardware present, deps OK).
    // Polled by the menu every frame so modules can become unavailable at runtime.
    virtual bool isAvailable() = 0;

    // Begin running. Implementations typically spawn a FreeRTOS task on Core 0.
    virtual void start() = 0;

    // Request the module to stop. Must be idempotent and quick.
    virtual void stop() = 0;

    // Short label used in the status bar while running.
    virtual const char* name() = 0;

    // Optional: fill a short stats string (≤21 chars) shown on the running screen.
    virtual void fillStats(char* /*buf*/, size_t /*len*/) {}

    // Optional: called when encoder LEFT/RIGHT fires while this module is running.
    // ev is encoder::InputEvent cast to uint8_t.
    virtual void onEvent(uint8_t /*ev*/) {}
};
