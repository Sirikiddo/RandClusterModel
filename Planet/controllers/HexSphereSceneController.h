#pragma once

#include <QElapsedTimer>
#include <QSet>
#include <QVector3D>
#include <QtDebug>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include "controllers/PathBuilder.h"
#include "generation/MeshGenerators/TerrainMeshGenerator.h"
#include "generation/MeshGenerators/WaterMeshGenerator.h"
#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

struct CachedTriangle {
    QVector3D center;      // Центр треугольника в world-space
    uint32_t i0, i1, i2;   // Индексы вершин
    float cachedDot;        // Кэшированное значение dot product (опционально)
};

struct VisibilityConfig {
    float baseThreshold = 0.1f;        // Базовый порог движения
    float farDistance = 5.0f;           // Расстояние, с которого начинается "далеко"
    float nearDistance = 2.0f;          // Расстояние, с которого начинается "близко"
    float fastSpeed = 1.0f;             // Порог быстрой скорости
    float mediumSpeed = 0.1f;           // Порог средней скорости
    float fastUpdateInterval = 0.2f;     // Интервал обновления при быстрой скорости (сек)
    float mediumUpdateInterval = 0.5f;   // Интервал при средней скорости
    float slowUpdateInterval = 1.0f;     // Интервал при медленной скорости
    float forceUpdateDistance = 2.0f;    // Принудительное обновление при таком перемещении
};

struct VisibilityPrediction {
    std::vector<uint32_t> indicesNow;        // Для текущей позиции
    std::vector<uint32_t> indicesPredicted;  // Для предсказанной позиции
    QVector3D predictedPos;                   // Предсказанная позиция
    float predictionTime = 0.1f;               // Время предсказания (сек)
    bool usePrediction = false;                // Флаг использования предсказания
};



class HexSphereSceneController {
public:
    HexSphereSceneController();

    void setGenerator(std::unique_ptr<ITerrainGenerator> generator);
    void setGeneratorByIndex(int idx);
    void setGenParams(const TerrainParams& params);
    void setSubdivisionLevel(int level);
    void regenerateTreePlacements();

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

    float cellSize() const;
    bool isCellOccupiedByTree(int cellId) const;
    const std::vector<TreePlacement>& getTreePlacements() const { return treePlacements_; }
    void generateTreePlacements();

    void setCameraPosition(const QVector3D& pos) { cameraPos_ = pos; }
    QVector3D getCameraPosition() const { return cameraPos_; }
    QVector3D getPlanetCenter() const { return QVector3D(0, 0, 0); }

    bool hasCameraMoved(float distanceThreshold = 0.1f) const {
        float distSq = (cameraPos_ - lastCameraPos_).lengthSquared();
        float adaptiveThreshold = distanceThreshold;
        float camDist = cameraPos_.length();
        if (camDist > 5.0f) {
            adaptiveThreshold *= (camDist / 5.0f);
        }
        else if (camDist < 2.0f) {
            adaptiveThreshold *= (camDist / 2.0f);
        }
        adaptiveThreshold = std::max(adaptiveThreshold, 0.05f);

        float thresholdSq = adaptiveThreshold * adaptiveThreshold;
        bool moved = distSq > thresholdSq;

        if (moved) {
            auto stats = getVisibilityStats();
            qDebug() << "Camera moved. Dist:" << sqrt(distSq)
                << "Adaptive threshold:" << adaptiveThreshold
                << "Visible:" << stats.first << "/" << stats.second;
        }

        return moved;
    }
    void updateLastCameraPosition() { lastCameraPos_ = cameraPos_; }
    std::vector<uint32_t> getVisibleIndices(const QVector3D& cameraPos) const;
    TerrainMesh getVisibleTerrainMesh() const;
    void updateVisibility(const QVector3D& cameraPos);
    std::pair<size_t, size_t> getVisibilityStats() const;

    bool shouldUpdateVisibility() const {
        if (!speedTimerStarted_) {
            speedTimer_.start();
            speedTimerStarted_ = true;
            return true;
        }
        float dt = speedTimer_.elapsed() / 1000.0f;
        if (dt > 0.1f) {
            QVector3D newVelocity = (cameraPos_ - lastCameraPos_) / dt;
            velocity_ = velocity_ * 0.7f + newVelocity * 0.3f;
            speedTimer_.restart();
        }

        float speed = velocity_.length();
        if (!lastUpdateStarted_) {
            lastUpdateTimer_.start();
            lastUpdateStarted_ = true;
            return true;
        }

        float timeSinceLastUpdate = lastUpdateTimer_.elapsed() / 1000.0f;

        bool needUpdate = false;
        if (speed > visibilityConfig_.fastSpeed) {
            needUpdate = timeSinceLastUpdate > visibilityConfig_.fastUpdateInterval;
        }
        else if (speed > visibilityConfig_.mediumSpeed) {
            needUpdate = timeSinceLastUpdate > visibilityConfig_.mediumUpdateInterval;
        }
        else {
            needUpdate = timeSinceLastUpdate > visibilityConfig_.slowUpdateInterval;
        }

        float distFromLast = (cameraPos_ - lastCameraPos_).length();
        if (distFromLast > visibilityConfig_.forceUpdateDistance) {
            needUpdate = true;
        }

        if (needUpdate) {
            lastUpdateTimer_.restart();
            qDebug() << "Updating visibility - Speed:" << speed
                << "Time since last:" << timeSinceLastUpdate;
        }

        return needUpdate;
    }
    void resetMotionDetector() {
        speedTimerStarted_ = false;
        velocity_ = QVector3D(0, 0, 0);
    }
    void setVisibilityConfig(const VisibilityConfig& config) { visibilityConfig_ = config; }
    const VisibilityConfig& getVisibilityConfig() const { return visibilityConfig_; }

private:
    float autoHeightStep() const;
    void updateTerrainMesh();

    void updateTreeOccupiedCells();

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

    std::vector<TreePlacement> treePlacements_;
    QSet<int> treeOccupiedCells_;

    QVector3D cameraPos_{ 0, 0, 5 };      // Текущая позиция камеры (начальное значение)
    QVector3D lastCameraPos_{ 0, 0, 5 };  // Позиция на прошлом кадре для детекта движения
    mutable std::vector<CachedTriangle> triangleCache_;
    mutable bool cacheValid_ = false;
    mutable QVector3D lastCacheCameraPos_;

    void rebuildCache() const;
    void validateCache() const;

    // Новые поля для детектора скорости
    mutable QVector3D velocity_;
    mutable QElapsedTimer speedTimer_;
    mutable bool speedTimerStarted_ = false;

    mutable QElapsedTimer lastUpdateTimer_;
    mutable bool lastUpdateStarted_ = false;

    VisibilityConfig visibilityConfig_;
};
