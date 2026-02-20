#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "generation/TerrainGenerator.h"

struct WorkOrder {
    bool rebuildMesh = false;
    bool regenerateTerrain = false;
    bool uploadBuffers = false;

    std::optional<int> newLevel{};
    std::optional<int> generatorIndex{};
    std::optional<TerrainParams> terrainParams{};
    std::optional<bool> smoothOneStep{};
    std::optional<float> stripInset{};
    std::optional<float> outlineBias{};
    std::vector<int> toggleCells{};

    // Boundary #1 semantics: coalesce per-cell toggles inside one work batch.
    // This gives deterministic "last command wins" behavior for duplicate cell ids.
    void queueToggleCell(int cellId) {
        toggleCells.erase(std::remove(toggleCells.begin(), toggleCells.end(), cellId), toggleCells.end());
        toggleCells.push_back(cellId);
    }

    bool hasWork() const {
        return rebuildMesh || regenerateTerrain || uploadBuffers || newLevel.has_value() ||
            generatorIndex.has_value() || terrainParams.has_value() ||
            smoothOneStep.has_value() || stripInset.has_value() ||
            outlineBias.has_value() || !toggleCells.empty();
    }
};
