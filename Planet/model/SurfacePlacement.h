#pragma once

#include "controllers/HexSphereSceneController.h"
#include <random>

inline QVector3D computeSurfacePoint(const HexSphereSceneController& scene, int cellId, float heightStep,
    float objectOffset = 0.03f) {
    const auto& cells = scene.model().cells();
    if (cellId < 0 || cellId >= static_cast<int>(cells.size())) {
        return QVector3D(0, 0, 1.0f);
    }

    const Cell& cell = cells[static_cast<size_t>(cellId)];

    // Радиус = базовый радиус (1.0) + высота ячейки * шаг высоты + смещение
    const float surfaceHeight = 1.0f + cell.height * heightStep;

    // Возвращаем нормализованное направление центроида, умноженное на радиус
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
    const float surfaceHeight = 1.0f + cell.height * heightStep;

    QVector3D posOnSphere = placement.getPosition(scene.model());
    return posOnSphere.normalized() * surfaceHeight;
}