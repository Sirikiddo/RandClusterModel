#pragma once

#include "controllers/HexSphereSceneController.h"

/**
 * Вычисляет точку на поверхности планеты для заданной ячейки
 *
 * @param scene - контроллер сцены
 * @param cellId - ID ячейки
 * @param heightStep - шаг высоты (обычно scene.heightStep())
 * @param objectOffset - небольшое смещение наружу для избежания z-fighting (по умолчанию 0.001f)
 * @return QVector3D позиция в мировых координатах
 */
inline QVector3D computeSurfacePoint(const HexSphereSceneController& scene, int cellId, float heightStep,
    float objectOffset = 0.001f) {
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