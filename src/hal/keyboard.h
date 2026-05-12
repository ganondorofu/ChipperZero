#pragma once

#include <stdint.h>
#include <stddef.h>

namespace keyboard {

constexpr uint8_t kMaxLen = 31;

struct State {
    char    buf[kMaxLen + 1] = {};
    uint8_t len     = 0;
    uint8_t charIdx = 0;
    bool    done    = false;
    bool    ok      = false;
};

void init(State& s);
void handleEvent(State& s, uint8_t ev);
void render(const State& s, const char* prompt = nullptr);

}  // namespace keyboard
