#pragma once
#include <cstdint>

struct DebugOverlay {
    uint64_t sceneVersion = 0;   // пока 0, со 2-го коммита начнет расти
    bool     hasPlan = false;
    bool     asyncBusy = false;

    // чисто для жизни
    float dtMs = 0.0f;
    float fps = 0.0f;
};