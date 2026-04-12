#include "LegacyTerrainBackend.h"

void LegacyTerrainBackend::attachTerrainBridge(ITerrainSceneBridge* bridge) {
    bridge_ = bridge;
}

void LegacyTerrainBackend::initializeTerrainState() {
    syncFromAdapter();
}

void LegacyTerrainBackend::setTerrainParams(const TerrainParams& params) {
    params_ = params;
    if (!bridge_) {
        return;
    }

    bridge_->stageTerrainParams(params);
    syncFromAdapter();
}

void LegacyTerrainBackend::setGeneratorByIndex(int idx) {
    generatorIndex_ = normalizeTerrainGeneratorIndex(idx);
    if (!bridge_) {
        return;
    }

    bridge_->stageGeneratorByIndex(generatorIndex_);
    syncFromAdapter();
}

void LegacyTerrainBackend::setSubdivisionLevel(int level) {
    subdivisionLevel_ = level;
    if (!bridge_) {
        return;
    }

    bridge_->stageSubdivisionLevel(level);
    syncFromAdapter();
}

void LegacyTerrainBackend::setTerrainRenderConfig(const TerrainRenderConfig& config) {
    renderConfig_ = config;
}

TerrainRegenerationResult LegacyTerrainBackend::regenerateTerrain() {
    if (!bridge_) {
        return TerrainRegenerationResult::failure("Terrain bridge is not attached");
    }

    bridge_->rebuildTerrainFromInputs();
    syncFromAdapter();
    return TerrainRegenerationResult::success();
}

bool LegacyTerrainBackend::prepareTerrainMesh() {
    return false;
}

bool LegacyTerrainBackend::prepareVisibleTerrainIndices(const QVector3D&) {
    return false;
}

const TerrainSnapshot* LegacyTerrainBackend::currentTerrainSnapshot() const {
    if (!currentSnapshot_) {
        return nullptr;
    }
    return &*currentSnapshot_;
}

const TerrainMesh* LegacyTerrainBackend::currentTerrainMesh() const {
    return nullptr;
}

const std::vector<uint32_t>* LegacyTerrainBackend::currentVisibleTerrainIndices() const {
    return nullptr;
}

void LegacyTerrainBackend::syncFromSnapshot(const TerrainSnapshot& snapshot) {
    params_ = snapshot.params;
    generatorIndex_ = normalizeTerrainGeneratorIndex(snapshot.generatorIndex);
    subdivisionLevel_ = snapshot.subdivisionLevel;
    currentSnapshot_ = snapshot;
}

void LegacyTerrainBackend::syncFromAdapter() {
    if (!bridge_) {
        return;
    }
    syncFromSnapshot(bridge_->captureTerrainSnapshot());
}
