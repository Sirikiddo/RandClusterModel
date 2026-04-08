#pragma once

#include <memory>

#include "DebugOverlay.h"
#include "TerrainBackendContract.h"

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

    const TerrainSnapshot* currentTerrainSnapshot() const;

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
