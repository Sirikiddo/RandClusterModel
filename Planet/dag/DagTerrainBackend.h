#pragma once

#include <memory>

#include "TerrainBackendContract.h"

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
    TerrainRegenerationResult regenerateTerrain();

    const TerrainSnapshot* currentTerrainSnapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
