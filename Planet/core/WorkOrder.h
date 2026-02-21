#pragma once

#include <optional>

#include "generation/TerrainGenerator.h"

struct WorkOrder {
    // Heavy synchronous work only: may rebuild meshes/regenerate terrain/uploadScene.
    bool rebuildMesh = false;
    bool regenerateTerrain = false;
    bool uploadBuffers = false;

    std::optional<int> newLevel{};
    std::optional<int> generatorIndex{};
    std::optional<TerrainParams> terrainParams{};
    std::optional<bool> smoothOneStep{};
    std::optional<float> stripInset{};
    std::optional<float> outlineBias{};

    bool hasWork() const {
        return rebuildMesh || regenerateTerrain || uploadBuffers || newLevel.has_value() ||
            generatorIndex.has_value() || terrainParams.has_value() ||
            smoothOneStep.has_value() || stripInset.has_value() ||
            outlineBias.has_value();
    }
};
