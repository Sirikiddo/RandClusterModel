#pragma once

#include <vector>

#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

enum class BackendMode {
    Legacy = 0,
    Mixed = 1,
    DagTerrainOnly = 2,
};

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
