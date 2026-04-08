#pragma once

#include <optional>

#include "TerrainBackendContract.h"

class LegacyTerrainBackend {
public:
    static constexpr bool usesDagPath = false;

    void attachTerrainBridge(ITerrainSceneBridge* bridge);
    void initializeTerrainState();

    void setTerrainParams(const TerrainParams& params);
    void setGeneratorByIndex(int idx);
    void setSubdivisionLevel(int level);
    TerrainRegenerationResult regenerateTerrain();

    const TerrainSnapshot* currentTerrainSnapshot() const;

private:
    void syncFromSnapshot(const TerrainSnapshot& snapshot);
    void syncFromAdapter();

    ITerrainSceneBridge* bridge_ = nullptr;
    TerrainParams params_{ 12345u, 3, 3.0f };
    int generatorIndex_ = 3;
    int subdivisionLevel_ = 2;
    std::optional<TerrainSnapshot> currentSnapshot_;
};
