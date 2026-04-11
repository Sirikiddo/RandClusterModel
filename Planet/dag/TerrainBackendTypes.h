#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

struct TerrainCellSnapshot {
    int height = 0;
    Biome biome = Biome::Grass;
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;
    float oreDensity = 0.0f;
    uint8_t oreType = 0;
    OreVisualParams oreVisual{};
    float oreNoiseOffset = 0.0f;
};

struct TerrainSnapshot {
    int subdivisionLevel = 2;
    int generatorIndex = 3;
    TerrainParams params{};
    std::vector<TerrainCellSnapshot> cells;

    bool empty() const noexcept {
        return cells.empty();
    }
};

struct TerrainRegenerationResult {
    bool ok = true;
    std::string message;

    explicit operator bool() const noexcept {
        return ok;
    }

    static TerrainRegenerationResult success() {
        return {};
    }

    static TerrainRegenerationResult failure(std::string text) {
        TerrainRegenerationResult result;
        result.ok = false;
        result.message = std::move(text);
        return result;
    }
};

struct TerrainDagStats {
    uint64_t terrainBuildCount = 0;
    uint64_t meshBuildCount = 0;
    uint64_t visibilityBuildCount = 0;
    size_t visibleIndexCount = 0;
};
