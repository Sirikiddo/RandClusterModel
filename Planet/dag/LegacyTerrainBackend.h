#pragma once

#include <optional>
#include <vector>

#include <QVector3D>

#include "TerrainBackendContract.h"
#include "renderers/TerrainTessellator.h"

class LegacyTerrainBackend {
public:
    static constexpr bool usesDagPath = false;

    void attachTerrainBridge(ITerrainSceneBridge* bridge);
    void initializeTerrainState();

    void setTerrainParams(const TerrainParams& params);
    void setGeneratorByIndex(int idx);
    void setSubdivisionLevel(int level);
    void setTerrainRenderConfig(const TerrainRenderConfig& config);
    TerrainRegenerationResult regenerateTerrain();
    bool prepareTerrainMesh();
    bool prepareVisibleTerrainIndices(const QVector3D& cameraPos);

    const TerrainSnapshot* currentTerrainSnapshot() const;
    const TerrainMesh* currentTerrainMesh() const;
    const std::vector<uint32_t>* currentVisibleTerrainIndices() const;

private:
    void syncFromSnapshot(const TerrainSnapshot& snapshot);
    void syncFromAdapter();

    ITerrainSceneBridge* bridge_ = nullptr;
    TerrainParams params_{ 12345u, 3, 3.0f };
    int generatorIndex_ = 3;
    int subdivisionLevel_ = 2;
    TerrainRenderConfig renderConfig_{};
    std::optional<TerrainSnapshot> currentSnapshot_;
};
