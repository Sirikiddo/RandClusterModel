#pragma once

#include <memory>

#include "DebugOverlay.h"
#include "TerrainBackendTypes.h"

class ITerrainBackendAdapter {
public:
    virtual ~ITerrainBackendAdapter() = default;

    virtual void legacySetTerrainParams(const TerrainParams& params) = 0;
    virtual void legacySetGeneratorByIndex(int idx) = 0;
    virtual void legacySetSubdivisionLevel(int level) = 0;
    virtual void legacyRegenerateTerrain() = 0;
    virtual TerrainSnapshot captureTerrainSnapshot() const = 0;
    virtual void applyTerrainSnapshot(const TerrainSnapshot& snapshot) = 0;
};

class EngineFacade {
public:
    EngineFacade();
    ~EngineFacade();

    EngineFacade(EngineFacade&&) noexcept;
    EngineFacade& operator=(EngineFacade&&) noexcept;
    EngineFacade(const EngineFacade&) = delete;
    EngineFacade& operator=(const EngineFacade&) = delete;

    void attachTerrainAdapter(ITerrainBackendAdapter* adapter);
    void initializeTerrainState();

    BackendMode backendMode() const;
    bool usesDagTerrainPath() const;

    void setTerrainParams(const TerrainParams& params);
    void setGeneratorByIndex(int idx);
    void setSubdivisionLevel(int level);
    bool regenerateTerrain();

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
