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

TerrainRegenerationResult LegacyTerrainBackend::regenerateTerrain() {
    if (!bridge_) {
        return TerrainRegenerationResult::failure("Terrain bridge is not attached");
    }

    bridge_->rebuildTerrainFromInputs();
    syncFromAdapter();
    return TerrainRegenerationResult::success();
}

const TerrainSnapshot* LegacyTerrainBackend::currentTerrainSnapshot() const {
    if (!currentSnapshot_) {
        return nullptr;
    }
    return &*currentSnapshot_;
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
