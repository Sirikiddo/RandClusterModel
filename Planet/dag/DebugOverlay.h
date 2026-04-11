#pragma once
#include <cstdint>

struct DebugOverlay {
    uint64_t sceneVersion = 0;
    bool     hasPlan = false;
    bool     asyncBusy = false;

    float dtMs = 0.0f;
    float fps = 0.0f;
    uint64_t terrainBuildCount = 0;
    uint64_t meshBuildCount = 0;
    uint64_t visibilityBuildCount = 0;
};
