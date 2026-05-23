#pragma once

#include <algorithm>

#include "TerrainMeshGenerator.h"

struct TerrainRenderConfig {
    bool smoothOneStep = true;
    float inset = 0.25f;
};

class TerrainMeshPolicy {
public:
    static float heightStepForSubdivision(int subdivisionLevel) {
        return 0.05f / (1.0f + static_cast<float>(subdivisionLevel) * 0.4f);
    }

    static TerrainMeshOptions buildOptions(int subdivisionLevel, const TerrainRenderConfig& config) {
        TerrainMeshOptions options;
        options.heightStep = heightStepForSubdivision(subdivisionLevel);
        options.inset = std::clamp(config.inset, 0.0f, 0.49f);
        options.smoothOneStep = config.smoothOneStep;
        return options;
    }
};
