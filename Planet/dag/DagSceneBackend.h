#pragma once

#include <memory>
#include <vector>

#include <QString>
#include <QVector3D>

#include "TerrainBackendTypes.h"
#include "model/HexSphereModel.h"

struct SelectionOutlineSnapshot {
    std::vector<float> vertices;
};

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
    std::vector<int> selectedCells;
    float heightStep = 0.0f;
    float outlineBias = 0.0f;
    bool smoothOneStep = true;
    std::vector<ModelPlacementRequest> modelRequests;
};

struct SceneDagResult {
    SelectionOutlineSnapshot selectionOutline;
    std::vector<TreePlacement> treePlacements;
    std::vector<ModelPlacement> modelPlacements;
};

struct DagDebugStats {
    int executedNodes = 0;
    int skippedGuardNodes = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
};

class DagSceneBackend {
public:
    DagSceneBackend();
    ~DagSceneBackend();

    DagSceneBackend(DagSceneBackend&&) noexcept;
    DagSceneBackend& operator=(DagSceneBackend&&) noexcept;
    DagSceneBackend(const DagSceneBackend&) = delete;
    DagSceneBackend& operator=(const DagSceneBackend&) = delete;

    SceneDagResult rebuild(const SceneDagRequest& request);
    const DagDebugStats& lastStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
