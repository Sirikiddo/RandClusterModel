#pragma once

#include <optional>
#include <vector>

#include "generation/TerrainGenerator.h"

struct WorkOrder {
    bool regenerateTerrain = false;

    std::optional<int> newLevel{};
    std::optional<int> generatorIndex{};
    std::optional<TerrainParams> terrainParams{};
    std::optional<bool> smoothOneStep{};
    std::optional<float> stripInset{};
    std::optional<float> outlineBias{};
    std::vector<int> toggleCells{};

    bool hasWork() const {
        return regenerateTerrain || newLevel.has_value() || generatorIndex.has_value() ||
            terrainParams.has_value() || smoothOneStep.has_value() || stripInset.has_value() ||
            outlineBias.has_value() || !toggleCells.empty();
    }
};
