#pragma once

#include <vector>

#include "model/HexSphereModel.h"

class WireMeshGenerator {
public:
    static std::vector<float> buildWireVertices(const HexSphereModel& model);
};
