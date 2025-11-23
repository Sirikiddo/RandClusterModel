#pragma once

#include "TerrainTessellator.h"
#include "model/HexSphereModel.h"

struct TerrainMeshOptions {
    float radius = 1.0f;
    float heightStep = 0.05f;
    float inset = 0.25f;
    bool smoothOneStep = true;
    float outerTrim = 0.15f;
    bool doCaps = true;
    bool doBlades = true;
    bool doCornerTris = true;
    bool doEdgeCliffs = true;
};

class TerrainMeshGenerator {
public:
    static TerrainMesh buildTerrainMesh(const HexSphereModel& model, const TerrainMeshOptions& options);
};
