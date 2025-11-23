#pragma once

#include <vector>

#include "HexSphereModel.h"

class WireMeshGenerator {
public:
    static std::vector<float> buildWireVertices(const HexSphereModel& model);
};
