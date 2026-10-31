#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "model/HexSphereTopology.h"
#include "model/HexSphereModel.h"

struct OreGenerationParams {
    float scale = 3.5f;
    float depositThreshold = 0.58f;
    float richThreshold = 0.80f;
};

class OreGenerator {
public:
    static void generate(HexSphereModel& model, uint32_t terrainSeed,
        const OreGenerationParams& params = {});
    static void clear(HexSphereModel& model);
    static void generate(
        const HexSphereTopology& topology,
        std::vector<TerrainCellState>& state,
        uint32_t terrainSeed,
        const OreGenerationParams& params = {});
    static void clear(std::vector<TerrainCellState>& state);
};
