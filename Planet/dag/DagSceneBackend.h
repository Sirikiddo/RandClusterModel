#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <QString>
#include <QVector3D>

#include "TerrainBackendTypes.h"
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"
#include "model/HexSphereModel.h"

struct SelectionDagResult {
    std::vector<float> vertices;
    bool success = false;
    int inputBytes = 0;
};

QString serializeSelectionOutlineInput(const SelectionOutlineInput& input);
std::optional<SelectionOutlineInput> deserializeSelectionOutlineInput(const QString& encoded);

struct ModelPlacementRequest {
    int entityId = -1;
    QString meshId;
    int cellId = -1;
    bool selected = false;
    float surfaceOffset = 0.0f;
};

struct ModelPlacement {
    int entityId = -1;
    QString meshId;
    int cellId = -1;
    bool selected = false;
    bool valid = false;
    QVector3D position;
    QVector3D up;
};

struct SceneDagRequest {
    TerrainSnapshot terrain;
    float heightStep = 0.0f;
    std::vector<ModelPlacementRequest> modelRequests;
};

struct SceneDagResult {
    std::vector<TreePlacement> treePlacements;
    std::vector<ModelPlacement> modelPlacements;
};

struct DagDebugStats {
    int executedNodes = 0;
    int skippedGuardNodes = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    int planCacheHits = 0;
    int planCacheMisses = 0;
    int inputBytes = 0;
};

class DagSceneBackend {
public:
    DagSceneBackend();
    ~DagSceneBackend();

    DagSceneBackend(DagSceneBackend&&) noexcept;
    DagSceneBackend& operator=(DagSceneBackend&&) noexcept;
    DagSceneBackend(const DagSceneBackend&) = delete;
    DagSceneBackend& operator=(const DagSceneBackend&) = delete;

    SelectionDagResult rebuildSelectionOutline(const SelectionOutlineInput& input);
    SceneDagResult rebuild(const SceneDagRequest& request);
    const DagDebugStats& lastStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
