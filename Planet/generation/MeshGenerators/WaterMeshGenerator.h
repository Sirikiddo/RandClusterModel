#pragma once

#include <cstdint>
#include <vector>

#include "model/HexSphereModel.h"

struct WaterGeometryData {
    std::vector<float> positions;
    std::vector<uint32_t> indices;
};

class WaterMeshGenerator {
public:
    static WaterGeometryData buildWaterGeometry(
        const HexSphereModel& model);
};
