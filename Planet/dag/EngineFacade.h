#pragma once

#include <memory>
#include <vector>

#include <QVector3D>

#include "DebugOverlay.h"
#include "TerrainBackendContract.h"
#include "renderers/TerrainTessellator.h"

class EngineFacade {
public:
    EngineFacade();
    ~EngineFacade();

    EngineFacade(EngineFacade&&) noexcept;
    EngineFacade& operator=(EngineFacade&&) noexcept;
    EngineFacade(const EngineFacade&) = delete;
    EngineFacade& operator=(const EngineFacade&) = delete;

    void attachTerrainBridge(ITerrainSceneBridge* bridge);
    void initializeTerrainState();

    void setTerrainParams(const TerrainParams& params);
    void setGeneratorByIndex(int idx);
    void setSubdivisionLevel(int level);
    TerrainRegenerationResult regenerateTerrain();
    void setVisibilityMesh(const TerrainMesh& mesh);
    bool prepareVisibleTerrainIndices(const QVector3D& cameraPos);

    const TerrainSnapshot* currentTerrainSnapshot() const;
    const TerrainMesh* currentTerrainMesh() const;
    const std::vector<uint32_t>* currentVisibleTerrainIndices() const;
    TerrainDagStats terrainDagStats() const;

    void tick(float dtSeconds);

    bool shouldRender() const { return true; }
    const DebugOverlay& overlay() const { return overlay_; }

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
    DebugOverlay overlay_;

    float fpsAccum_ = 0.0f;
    int fpsFrames_ = 0;
};
