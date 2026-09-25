#pragma once

#include "controllers/HexSphereSceneController.h"
#include <random>
#include <QDebug>

inline QVector3D computeSurfacePoint(const HexSphereSceneController& scene, int cellId, float heightStep,
    float objectOffset = 0.03f) {
    const auto& cells = scene.model().cells();
    if (cellId < 0 || cellId >= static_cast<int>(cells.size())) {
        return QVector3D(0, 0, 1.0f);
    }

    const Cell& cell = cells[static_cast<size_t>(cellId)];
    const float surfaceHeight = 1.0f + cell.height * heightStep;
    return cell.centroid.normalized() * (surfaceHeight + objectOffset);
}

inline QVector3D computeSurfacePoint(const HexSphereSceneController& scene, int cellId) {
    return computeSurfacePoint(scene, cellId, scene.heightStep());
}

inline QVector3D computeSurfacePoint(const HexSphereSceneController& scene, const TreePlacement& placement,
    float heightStep) {
    const auto& cells = scene.model().cells();
    if (placement.cellId < 0 || placement.cellId >= static_cast<int>(cells.size())) {
        return QVector3D(0, 0, 1.0f);
    }

    const Cell& cell = cells[static_cast<size_t>(placement.cellId)];

    if (placement.placementMode == TreePlacement::PlacementMode::World) {
        return placement.worldPosition;
    }

    QVector3D direction = placement.getPosition(scene.model());
    if (direction.lengthSquared() < 1e-8f) {
        direction = cell.centroid;
    }
    direction = direction.normalized();

    // ========== ÓÁÐÀËÈ surfaceOffset ==========
    const float surfaceHeight = 1.0f + cell.height * heightStep;

    return direction * surfaceHeight;
}