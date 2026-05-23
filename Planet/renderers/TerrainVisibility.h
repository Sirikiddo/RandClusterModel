#pragma once

#include <QElapsedTimer>
#include <QVector3D>

#include <vector>

#include "renderers/TerrainTessellator.h"

struct TerrainVisibilityUpdateConfig {
    float baseThreshold = 0.1f;
    float farDistance = 5.0f;
    float nearDistance = 2.0f;
    float fastSpeed = 1.0f;
    float mediumSpeed = 0.1f;
    float fastUpdateInterval = 0.2f;
    float mediumUpdateInterval = 0.5f;
    float slowUpdateInterval = 1.0f;
    float forceUpdateDistance = 2.0f;
};

std::vector<uint32_t> buildVisibleTerrainIndices(const TerrainMesh& mesh, const QVector3D& cameraPos);

class TerrainVisibilityController {
public:
    bool shouldUpdate(const QVector3D& cameraPos);
    void markVisibilityApplied(const QVector3D& cameraPos);
    void reset();

private:
    bool hasCameraMoved(const QVector3D& cameraPos, float distanceThreshold) const;

    QVector3D lastCameraPos_{ 0.0f, 0.0f, 5.0f };
    QVector3D velocity_{ 0.0f, 0.0f, 0.0f };
    bool speedTimerStarted_ = false;
    bool lastUpdateStarted_ = false;
    QElapsedTimer speedTimer_;
    QElapsedTimer lastUpdateTimer_;
    TerrainVisibilityUpdateConfig config_{};
};
