#pragma once

#include <QSet>
#include <QVector3D>
#include <optional>
#include <memory>
#include <vector>

#include "model/HexSphereModel.h"
#include "generation/TerrainGenerator.h"
#include "controllers/PathBuilder.h"
#include "generation/MeshGenerators/WaterMeshGenerator.h"
#include "generation/MeshGenerators/TerrainMeshGenerator.h"

class HexSphereSceneController {
public:
    HexSphereSceneController();

    void setGenerator(std::unique_ptr<ITerrainGenerator> generator);
    void setGeneratorByIndex(int idx);
    void setGenParams(const TerrainParams& params);
    void setSubdivisionLevel(int level);

    void setSmoothOneStep(bool on);
    void setStripInset(float value);
    void setOutlineBias(float value);

    void rebuildModel();
    void regenerateTerrain();

    void clearSelection();
    void toggleCellSelection(int cellId);

    std::optional<std::vector<QVector3D>> buildPathPolyline() const;

    std::vector<float> buildWireVertices() const;
    std::vector<float> buildSelectionOutlineVertices() const;
    WaterGeometryData buildWaterGeometry() const;

    const HexSphereModel& model() const { return model_; }
    HexSphereModel& modelMutable() { return model_; }
    const TerrainMesh& terrain() const { return terrainCPU_; }
    const QSet<int>& selectedCells() const { return selectedCells_; }

    int subdivisionLevel() const { return L_; }
    float heightStep() const { return heightStep_; }
    float outlineBias() const { return outlineBias_; }
    float stripInset() const { return stripInset_; }
    bool smoothOneStep() const { return smoothOneStep_; }
    float pathBias() const { return pathBias_; }

    // ========== НОВЫЕ МЕТОДЫ ДЛЯ РАБОТЫ С КАМЕРОЙ ==========

    // Установить позицию камеры (вызывается каждый кадр из рендерера)
    void setCameraPosition(const QVector3D& pos) { cameraPos_ = pos; }

    // Получить позицию камеры
    QVector3D getCameraPosition() const { return cameraPos_; }

    // Получить центр планеты (всегда 0,0,0 в world-space)
    QVector3D getPlanetCenter() const { return QVector3D(0, 0, 0); }

    // Проверить, изменилась ли камера с прошлого кадра
    //bool hasCameraMoved() const {
    //    return (cameraPos_ - lastCameraPos_).lengthSquared() > 0.0001f;
    //}

    bool hasCameraMoved() const {
        float distSq = (cameraPos_ - lastCameraPos_).lengthSquared();
        bool moved = distSq > 0.00001f;
        if (moved) {
            qDebug() << "Distance since last move:" << distSq;
        }
        return moved;
    }

    // Обновить "последнюю" позицию камеры (вызывать после обработки)
    void updateLastCameraPosition() { lastCameraPos_ = cameraPos_; }

    // ========== НОВЫЕ МЕТОДЫ ДЛЯ ФИЛЬТРАЦИИ ПО ВИДИМОСТИ ==========

    // Получить только видимые индексы на основе позиции камеры
    std::vector<uint32_t> getVisibleIndices(const QVector3D& cameraPos) const;

    // Получить TerrainMesh только с видимыми треугольниками
    TerrainMesh getVisibleTerrainMesh() const;

    // Обновить видимость (вызывается каждый кадр)
    void updateVisibility(const QVector3D& cameraPos);

    // Получить статистику: (видимых треугольников, всего треугольников)
    std::pair<size_t, size_t> getVisibilityStats() const;

private:
    float autoHeightStep() const;
    void updateTerrainMesh();

    IcosphereBuilder icoBuilder_;
    IcoMesh ico_;
    HexSphereModel model_;
    TerrainMesh terrainCPU_;

    std::unique_ptr<ITerrainGenerator> generator_;
    TerrainParams genParams_{};

    int L_ = 2;
    float heightStep_ = 0.06f;
    bool smoothOneStep_ = true;
    float outlineBias_ = 0.004f;
    float stripInset_ = 0.25f;
    float pathBias_ = 0.01f;

    QSet<int> selectedCells_;

    // ========== НОВЫЕ ПОЛЯ ДЛЯ РАБОТЫ С КАМЕРОЙ ==========
    QVector3D cameraPos_{ 0, 0, 5 };      // Текущая позиция камеры (начальное значение)
    QVector3D lastCameraPos_{ 0, 0, 5 };  // Позиция на прошлом кадре для детекта движения
};
