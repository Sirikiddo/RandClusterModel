#pragma once

#include <vector>
#include <cstdint>

#include "HexSphereModel.h"

// Данные для генерации воды перед загрузкой в GPU
struct WaterGeometryData {
    std::vector<float> positions;
    std::vector<float> edgeFlags;
    std::vector<uint32_t> indices;
};

class WaterMeshGenerator {
public:
    static WaterGeometryData buildWaterGeometry(const HexSphereModel& model);
};
