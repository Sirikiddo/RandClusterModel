#pragma once
#include <cstdint>

struct DebugOverlay {
    uint64_t sceneVersion = 0;

    // Strict semantic: pending HEAVY work only (unconsumed WorkOrder).
    // LightWork is applied synchronously in the same tick and is not tracked here.
    bool hasPendingWork = false;

    bool asyncBusy = false;

    float dtMs = 0.0f;
    float fps = 0.0f;
};
