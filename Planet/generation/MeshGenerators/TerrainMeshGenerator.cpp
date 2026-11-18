#include "TerrainMeshGenerator.h"
#include "renderers/TerrainTessellator.h"

TerrainMesh TerrainMeshGenerator::buildTerrainMesh(const HexSphereModel& model, const TerrainMeshOptions& options) {
    TerrainTessellator tt;
    tt.directWrite = options.directWrite;
    tt.inset = options.inset;
    tt.smoothMaxDelta = options.smoothOneStep ? 1 : 0;
    tt.outerTrim = options.outerTrim;
    tt.doCaps = options.doCaps;
    tt.doBlades = options.doBlades;
    tt.doCornerTris = options.doCornerTris;
    tt.doEdgeCliffs = options.doEdgeCliffs;
    tt.coastalBand = options.coastalBand;
    tt.waterParams = options.waterParams;
    tt.rockSeed = options.rockSeed;

    return tt.build(model);
}

