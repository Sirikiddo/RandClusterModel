#pragma once

#include <memory>

#include "DebugOverlay.h"
#include "TerrainBackendContract.h"
#include "DagPathBackend.h"
#include "DagSceneBackend.h"

class EngineFacade {
public:
    EngineFacade();
    ~EngineFacade();

    EngineFacade(EngineFacade&&) noexcept;
    EngineFacade& operator=(EngineFacade&&) noexcept;
    EngineFacade(const EngineFacade&) = delete;
    EngineFacade& operator=(const EngineFacade&) = delete;

    // ===== ТЕРРЕЙН =====
    void attachTerrainBridge(ITerrainSceneBridge* bridge);
    void initializeTerrainState();

    void setTerrainParams(const TerrainParams& params);
    void setGeneratorByIndex(int idx);
    void setSubdivisionLevel(int level);
    TerrainRegenerationResult regenerateTerrain();

    const TerrainSnapshot* currentTerrainSnapshot() const;

    // ===== ПОИСК ПУТИ =====

    /// Установить параметр сглаживания подъёмов
    void setPathSmoothMaxDelta(int delta);
    void setPathTerrainSnapshot(const TerrainSnapshot& snapshot);

    /// Найти путь между двумя ячейками
    PathResult findPath(int startCellId, int goalCellId);

    /// Получить последний результат поиска пути
    const PathResult& lastPathResult() const;

    // ===== ПРОИЗВОДНЫЕ ДАННЫЕ СЦЕНЫ =====
    SceneDagResult rebuildSceneDerived(const SceneDagRequest& request);
    const DagDebugStats& lastSceneDagStats() const;

    // ===== ОБЩЕЕ =====
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
