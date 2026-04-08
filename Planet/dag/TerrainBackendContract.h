#pragma once

#include <concepts>

#include "TerrainBackendTypes.h"

class ITerrainSceneBridge {
public:
    virtual ~ITerrainSceneBridge() = default;

    virtual void stageTerrainParams(const TerrainParams& params) = 0;
    virtual void stageGeneratorByIndex(int idx) = 0;
    virtual void stageSubdivisionLevel(int level) = 0;
    virtual void rebuildTerrainFromInputs() = 0;
    virtual TerrainSnapshot captureTerrainSnapshot() const = 0;
    virtual void projectTerrainSnapshot(const TerrainSnapshot& snapshot) = 0;
};

template <class T>
concept TerrainBackend = requires(T& backend, ITerrainSceneBridge* bridge, const TerrainParams& params, int idx, int level) {
    { T::usesDagPath } -> std::convertible_to<bool>;
    { backend.attachTerrainBridge(bridge) } -> std::same_as<void>;
    { backend.initializeTerrainState() } -> std::same_as<void>;
    { backend.setTerrainParams(params) } -> std::same_as<void>;
    { backend.setGeneratorByIndex(idx) } -> std::same_as<void>;
    { backend.setSubdivisionLevel(level) } -> std::same_as<void>;
    { backend.regenerateTerrain() } -> std::same_as<TerrainRegenerationResult>;
    { backend.currentTerrainSnapshot() } -> std::same_as<const TerrainSnapshot*>;
};
