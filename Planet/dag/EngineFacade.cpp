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

const TerrainSnapshot* EngineFacade::currentTerrainSnapshot() const {
    return impl_->backend.currentTerrainSnapshot();
}

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
