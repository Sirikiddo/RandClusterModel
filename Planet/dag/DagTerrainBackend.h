#pragma once

#include <memory>
#include <vector>

#include <QVector3D>

#include "TerrainBackendContract.h"
#include "renderers/TerrainTessellator.h"

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
    void setTerrainRenderConfig(const TerrainRenderConfig& config);
    TerrainRegenerationResult regenerateTerrain();
    bool prepareTerrainMesh();
    bool prepareVisibleTerrainIndices(const QVector3D& cameraPos);

    const TerrainSnapshot* currentTerrainSnapshot() const;
    const TerrainMesh* currentTerrainMesh() const;
    const std::vector<uint32_t>* currentVisibleTerrainIndices() const;
    TerrainDagStats debugStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
