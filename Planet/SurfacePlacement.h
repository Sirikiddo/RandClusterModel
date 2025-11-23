#pragma once

#include "HexSphereSceneController.h"

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
