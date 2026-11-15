#pragma once

#include <memory>

#include "TerrainBackendContract.h"

struct TerrainBuildStats {
    int geometryExecutions = 0;
    int terrainExecutions = 0;
    int oreExecutions = 0;
};

class DagTerrainBackend {
public:
    static constexpr bool usesDagPath = true;

    DagTerrainBackend();
    ~DagTerrainBackend();

    DagTerrainBackend(DagTerrainBackend&&) noexcept;
    DagTerrainBackend& operator=(DagTerrainBackend&&) noexcept;
    DagTerrainBackend(const DagTerrainBackend&) = delete;
    DagTerrainBackend& operator=(const DagTerrainBackend&) = delete;

    void attachTerrainBridge(ITerrainSceneBridge* bridge);
    void initializeTerrainState();

    void setTerrainParams(const TerrainParams& params);
    void setGeneratorByIndex(int idx);
    void setSubdivisionLevel(int level);
    void setGeometry(SphereGeometry geometry);
    TerrainRegenerationResult regenerateTerrain();

    const TerrainSnapshot* currentTerrainSnapshot() const;
    TerrainBuildStats lastBuildStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
