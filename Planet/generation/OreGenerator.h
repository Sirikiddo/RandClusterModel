#pragma once

#include <cstdint>

class HexSphereModel;

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
};
