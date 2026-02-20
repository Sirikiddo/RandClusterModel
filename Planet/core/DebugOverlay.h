#pragma once
#include <cstdint>

struct DebugOverlay {
    uint64_t sceneVersion = 0;

    // Strict semantic: true only while PlanetCore has unconsumed WorkOrder.
    // Does not represent future async execution state.
    bool hasPendingWork = false;

    bool asyncBusy = false;

    float dtMs = 0.0f;
    float fps = 0.0f;
};
