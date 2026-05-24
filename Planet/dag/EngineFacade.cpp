#include "EngineFacade.h"

#include <memory>
#include "TerrainBackendSelector.h"

// Расширяем Impl для поддержки PathBackend
struct EngineFacade::Impl {
    SelectedTerrainBackend terrainBackend;
    DagPathBackend pathBackend;
    DagSceneBackend sceneBackend;
};

static_assert(TerrainBackend<SelectedTerrainBackend>);

// ===== КОНСТРУКТОР / ДЕСТРУКТОР =====

EngineFacade::EngineFacade()
    : impl_(std::make_unique<Impl>()) {
    overlay_.hasPlan = kUsesDagTerrainBackend;
}

EngineFacade::~EngineFacade() {
    impl_.reset();
}

EngineFacade::EngineFacade(EngineFacade&&) noexcept = default;
EngineFacade& EngineFacade::operator=(EngineFacade&&) noexcept = default;

// ===== ТЕРРЕЙН =====

void EngineFacade::attachTerrainBridge(ITerrainSceneBridge* bridge) {
    impl_->terrainBackend.attachTerrainBridge(bridge);
}

void EngineFacade::initializeTerrainState() {
    impl_->terrainBackend.initializeTerrainState();
    overlay_.hasPlan = kUsesDagTerrainBackend;
}

void EngineFacade::setTerrainParams(const TerrainParams& params) {
    impl_->terrainBackend.setTerrainParams(params);
}

void EngineFacade::setGeneratorByIndex(int idx) {
    impl_->terrainBackend.setGeneratorByIndex(idx);
}

void EngineFacade::setSubdivisionLevel(int level) {
    impl_->terrainBackend.setSubdivisionLevel(level);
}

TerrainRegenerationResult EngineFacade::regenerateTerrain() {
    const auto result = impl_->terrainBackend.regenerateTerrain();

    if (result) {
        ++overlay_.sceneVersion;

        // После успешной регенерации террейна обновляем PathBackend
        const TerrainSnapshot* snapshot = impl_->terrainBackend.currentTerrainSnapshot();
        if (snapshot) {
            impl_->pathBackend.setTerrainSnapshot(*snapshot);
        }
    }

    return result;
}

const TerrainSnapshot* EngineFacade::currentTerrainSnapshot() const {
    return impl_->terrainBackend.currentTerrainSnapshot();
}

// ===== ПОИСК ПУТИ =====

void EngineFacade::setPathSmoothMaxDelta(int delta) {
    impl_->pathBackend.setSmoothMaxDelta(delta);
}

void EngineFacade::setPathTerrainSnapshot(const TerrainSnapshot& snapshot) {
    impl_->pathBackend.setTerrainSnapshot(snapshot);
}

PathResult EngineFacade::findPath(int startCellId, int goalCellId) {
    return impl_->pathBackend.findPath(startCellId, goalCellId);
}

const PathResult& EngineFacade::lastPathResult() const {
    return impl_->pathBackend.lastResult();
}

// ===== ПРОИЗВОДНЫЕ ДАННЫЕ СЦЕНЫ =====

SceneDagResult EngineFacade::rebuildSceneDerived(const SceneDagRequest& request) {
    return impl_->sceneBackend.rebuild(request);
}

const DagDebugStats& EngineFacade::lastSceneDagStats() const {
    return impl_->sceneBackend.lastStats();
}

// ===== TICK =====

void EngineFacade::tick(float dtSeconds) {
    overlay_.dtMs = dtSeconds * 1000.0f;
    overlay_.hasPlan = kUsesDagTerrainBackend;

    fpsAccum_ += dtSeconds;
    ++fpsFrames_;
    if (fpsAccum_ >= 0.5f) {
        overlay_.fps = fpsFrames_ / fpsAccum_;
        fpsAccum_ = 0.0f;
        fpsFrames_ = 0;
    }
}
