#include "EngineFacade.h"

#include <memory>
#include "TerrainBackendSelector.h"

struct EngineFacade::Impl {
    SelectedTerrainBackend backend;
};

static_assert(TerrainBackend<SelectedTerrainBackend>);

EngineFacade::EngineFacade()
    : impl_(std::make_unique<Impl>()) {
    overlay_.hasPlan = kUsesDagTerrainBackend;
}

EngineFacade::~EngineFacade() {
    impl_.reset();
}
EngineFacade::EngineFacade(EngineFacade&&) noexcept = default;
EngineFacade& EngineFacade::operator=(EngineFacade&&) noexcept = default;

void EngineFacade::attachTerrainBridge(ITerrainSceneBridge* bridge) {
    impl_->backend.attachTerrainBridge(bridge);
}

void EngineFacade::initializeTerrainState() {
    impl_->backend.initializeTerrainState();
    overlay_.hasPlan = kUsesDagTerrainBackend;
}

void EngineFacade::setTerrainParams(const TerrainParams& params) {
    impl_->backend.setTerrainParams(params);
}

void EngineFacade::setGeneratorByIndex(int idx) {
    impl_->backend.setGeneratorByIndex(idx);
}

void EngineFacade::setSubdivisionLevel(int level) {
    impl_->backend.setSubdivisionLevel(level);
}

TerrainRegenerationResult EngineFacade::regenerateTerrain() {
    const auto result = impl_->backend.regenerateTerrain();
    if (result) {
        ++overlay_.sceneVersion;
    }
    return result;
}

void EngineFacade::setVisibilityMesh(const TerrainMesh& mesh) {
    impl_->backend.setVisibilityMesh(mesh);
}

bool EngineFacade::prepareVisibleTerrainIndices(const QVector3D& cameraPos) {
    return impl_->backend.prepareVisibleTerrainIndices(cameraPos);
}

const TerrainSnapshot* EngineFacade::currentTerrainSnapshot() const {
    return impl_->backend.currentTerrainSnapshot();
}

const TerrainMesh* EngineFacade::currentTerrainMesh() const {
    return impl_->backend.currentTerrainMesh();
}

const std::vector<uint32_t>* EngineFacade::currentVisibleTerrainIndices() const {
    return impl_->backend.currentVisibleTerrainIndices();
}

TerrainDagStats EngineFacade::terrainDagStats() const {
    return impl_->backend.debugStats();
}

void EngineFacade::tick(float dtSeconds) {
    overlay_.dtMs = dtSeconds * 1000.0f;
    overlay_.hasPlan = kUsesDagTerrainBackend;
    const TerrainDagStats stats = impl_->backend.debugStats();
    overlay_.terrainBuildCount = stats.terrainBuildCount;
    overlay_.meshBuildCount = stats.meshBuildCount;
    overlay_.visibilityBuildCount = stats.visibilityBuildCount;

    fpsAccum_ += dtSeconds;
    ++fpsFrames_;
    if (fpsAccum_ >= 0.5f) {
        overlay_.fps = fpsFrames_ / fpsAccum_;
        fpsAccum_ = 0.0f;
        fpsFrames_ = 0;
    }
}
