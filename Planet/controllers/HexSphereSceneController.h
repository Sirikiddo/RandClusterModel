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

#include "core/AppViewConfig.h"
#include "dag/TerrainBackendTypes.h"
#include "controllers/PathBuilder.h"
#include "generation/MeshGenerators/TerrainMeshGenerator.h"
#include "generation/MeshGenerators/WaterMeshGenerator.h"
#include "generation/TerrainGenerator.h"
#include "generation/WaterWaveModel.h"
#include "model/HexSphereModel.h"

struct CachedTriangle {
    QVector3D center;      // Ð¦ÐµÐ½Ñ‚Ñ€ Ñ‚Ñ€ÐµÑƒÐ³Ð¾Ð»ÑŒÐ½Ð¸ÐºÐ° Ð² world-space
    uint32_t i0, i1, i2;   // Ð˜Ð½Ð´ÐµÐºÑÑ‹ Ð²ÐµÑ€ÑˆÐ¸Ð½
    float cachedDot;        // ÐšÑÑˆÐ¸Ñ€Ð¾Ð²Ð°Ð½Ð½Ð¾Ðµ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ðµ dot product (Ð¾Ð¿Ñ†Ð¸Ð¾Ð½Ð°Ð»ÑŒÐ½Ð¾)
};

struct VisibilityConfig {
    float baseThreshold = 0.1f;        // Ð‘Ð°Ð·Ð¾Ð²Ñ‹Ð¹ Ð¿Ð¾Ñ€Ð¾Ð³ Ð´Ð²Ð¸Ð¶ÐµÐ½Ð¸Ñ
    float farDistance = 5.0f;           // Ð Ð°ÑÑÑ‚Ð¾ÑÐ½Ð¸Ðµ, Ñ ÐºÐ¾Ñ‚Ð¾Ñ€Ð¾Ð³Ð¾ Ð½Ð°Ñ‡Ð¸Ð½Ð°ÐµÑ‚ÑÑ "Ð´Ð°Ð»ÐµÐºÐ¾"
    float nearDistance = 2.0f;          // Ð Ð°ÑÑÑ‚Ð¾ÑÐ½Ð¸Ðµ, Ñ ÐºÐ¾Ñ‚Ð¾Ñ€Ð¾Ð³Ð¾ Ð½Ð°Ñ‡Ð¸Ð½Ð°ÐµÑ‚ÑÑ "Ð±Ð»Ð¸Ð·ÐºÐ¾"
    float fastSpeed = 1.0f;             // ÐŸÐ¾Ñ€Ð¾Ð³ Ð±Ñ‹ÑÑ‚Ñ€Ð¾Ð¹ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚Ð¸
    float mediumSpeed = 0.1f;           // ÐŸÐ¾Ñ€Ð¾Ð³ ÑÑ€ÐµÐ´Ð½ÐµÐ¹ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚Ð¸
    float fastUpdateInterval = 0.2f;     // Ð˜Ð½Ñ‚ÐµÑ€Ð²Ð°Ð» Ð¾Ð±Ð½Ð¾Ð²Ð»ÐµÐ½Ð¸Ñ Ð¿Ñ€Ð¸ Ð±Ñ‹ÑÑ‚Ñ€Ð¾Ð¹ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚Ð¸ (ÑÐµÐº)
    float mediumUpdateInterval = 0.5f;   // Ð˜Ð½Ñ‚ÐµÑ€Ð²Ð°Ð» Ð¿Ñ€Ð¸ ÑÑ€ÐµÐ´Ð½ÐµÐ¹ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚Ð¸
    float slowUpdateInterval = 1.0f;     // Ð˜Ð½Ñ‚ÐµÑ€Ð²Ð°Ð» Ð¿Ñ€Ð¸ Ð¼ÐµÐ´Ð»ÐµÐ½Ð½Ð¾Ð¹ ÑÐºÐ¾Ñ€Ð¾ÑÑ‚Ð¸
    float forceUpdateDistance = 2.0f;    // ÐŸÑ€Ð¸Ð½ÑƒÐ´Ð¸Ñ‚ÐµÐ»ÑŒÐ½Ð¾Ðµ Ð¾Ð±Ð½Ð¾Ð²Ð»ÐµÐ½Ð¸Ðµ Ð¿Ñ€Ð¸ Ñ‚Ð°ÐºÐ¾Ð¼ Ð¿ÐµÑ€ÐµÐ¼ÐµÑ‰ÐµÐ½Ð¸Ð¸
};

struct VisibilityPrediction {
    std::vector<uint32_t> indicesNow;        // Ð”Ð»Ñ Ñ‚ÐµÐºÑƒÑ‰ÐµÐ¹ Ð¿Ð¾Ð·Ð¸Ñ†Ð¸Ð¸
    std::vector<uint32_t> indicesPredicted;  // Ð”Ð»Ñ Ð¿Ñ€ÐµÐ´ÑÐºÐ°Ð·Ð°Ð½Ð½Ð¾Ð¹ Ð¿Ð¾Ð·Ð¸Ñ†Ð¸Ð¸
    QVector3D predictedPos;                   // ÐŸÑ€ÐµÐ´ÑÐºÐ°Ð·Ð°Ð½Ð½Ð°Ñ Ð¿Ð¾Ð·Ð¸Ñ†Ð¸Ñ
    float predictionTime = 0.1f;               // Ð’Ñ€ÐµÐ¼Ñ Ð¿Ñ€ÐµÐ´ÑÐºÐ°Ð·Ð°Ð½Ð¸Ñ (ÑÐµÐº)
    bool usePrediction = false;                // Ð¤Ð»Ð°Ð³ Ð¸ÑÐ¿Ð¾Ð»ÑŒÐ·Ð¾Ð²Ð°Ð½Ð¸Ñ Ð¿Ñ€ÐµÐ´ÑÐºÐ°Ð·Ð°Ð½Ð¸Ñ
};



class HexSphereSceneController {
public:
    explicit HexSphereSceneController(SceneViewMode viewMode = SceneViewMode::Planet);

    void setGeneratorByIndex(int idx);
    void setGenParams(const TerrainParams& params);
    WaterUpdateKind setWaterParams(const WaterParams& params);
    void setSubdivisionLevel(int level);
    void stageSubdivisionLevel(int level);
    void rebuildTerrainFromInputs();
    void regenerateTreePlacements();

    void setSmoothOneStep(bool on);
    void setStripInset(float value);
    void setOutlineBias(float value);

    void rebuildModel();
    void regenerateTerrain();
    void rebuildDerivedGeometry();
    void rebuildTerrainPresentation();
    void clearForShutdown();

    void clearSelection();
    void toggleCellSelection(int cellId);
    void setSelectionOutlineVertices(std::vector<float> vertices);
    void setTreePlacements(std::vector<TreePlacement> placements);

    std::vector<QVector3D> buildPathPolyline(const std::vector<int>& path) const;
    std::vector<float> buildRoadMesh(const std::vector<int>& path) const;

    std::vector<float> buildWireVertices() const;
    std::vector<float> buildSelectionOutlineVertices() const;
    std::vector<float> buildOutlineVerticesForCells(const QSet<int>& cells) const;
    TerrainSnapshot captureTerrainSnapshot() const;
    void applyTerrainSnapshot(const TerrainSnapshot& snapshot);

    const HexSphereModel& model() const { return model_; }
    HexSphereModel& modelMutable() { return model_; }
    const TerrainMesh& terrain() const { return terrainCPU_; }
    const WaterGeometryData& waterGeometry() const { return waterCPU_; }
    uint64_t waterProxyRevision() const { return waterProxyRevision_; }
    const WaterParams& waterParams() const { return waterParams_; }
    const WaterParams& resolvedWaterParamsValue() const { return resolvedWaterParams_; }
    const CoastalBandData& coastalBand() const { return coastalBand_; }
    const ResolvedWaterWaveSpec& resolvedWaterWaveSpec() const { return resolvedWaterWaveSpec_; }
    const QSet<int>& selectedCells() const { return selectedCells_; }

    int subdivisionLevel() const { return L_; }
    int generatorIndex() const { return generatorIndex_; }
    float heightStep() const { return heightStep_; }
    float outlineBias() const { return outlineBias_; }
    float stripInset() const { return stripInset_; }
    bool smoothOneStep() const { return smoothOneStep_; }
    float pathBias() const { return pathBias_; }

    float cellSize() const;
    float getModelScaleFactor() const;
    bool isCellOccupiedByTree(int cellId) const;
    const std::vector<TreePlacement>& getTreePlacements() const { return treePlacements_; }
    void generateTreePlacements();
    SceneViewMode sceneViewMode() const { return viewMode_; }
    bool isContributorMode() const { return viewMode_ == SceneViewMode::Contributor; }
    bool supportsTerrainVisibility() const { return !isContributorMode() && !terrainCPU_.idx.empty(); }

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
        return distSq > thresholdSq;
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
        }

        return needUpdate;
    }
    void resetMotionDetector() {
        speedTimerStarted_ = false;
        velocity_ = QVector3D(0, 0, 0);
    }
    void setVisibilityConfig(const VisibilityConfig& config) { visibilityConfig_ = config; }
    const VisibilityConfig& getVisibilityConfig() const { return visibilityConfig_; }
    void rebuildPickTris();
    int findCellByPosition(const QVector3D& position) const;

private:
    float autoHeightStep() const;
    void rebuildTopology();
    void rebuildWaterProxy();
    void updateTerrainMesh();
    void buildTerrainMeshFromCurrentCoast();
    void refreshResolvedWaterParams();
    void refreshResolvedWaterState();
    void rebuildCoastalBand();
    void rebuildContributorScene();

    void updateTreeOccupiedCells();

    SceneViewMode viewMode_ = SceneViewMode::Planet;

    IcosphereBuilder icoBuilder_;
    IcoMesh ico_;
    HexSphereModel model_;
    TerrainMesh terrainCPU_;
    WaterGeometryData waterCPU_;
    uint64_t waterProxyRevision_ = 0;
    CoastalBandData coastalBand_;
    WaterParams waterParams_ = waterParamsForPreset(WaterPreset::Temperate);
    WaterParams resolvedWaterParams_ = waterParams_;
    ResolvedWaterWaveSpec resolvedWaterWaveSpec_{};

    TerrainParams genParams_ = defaultTerrainParams();
    int generatorIndex_ = kDefaultTerrainGeneratorIndex;
    std::unique_ptr<ITerrainGenerator> generator_;

    int L_ = kDefaultTerrainSubdivisionLevel;
    bool topologyDirty_ = false;
    float heightStep_ = 0.06f;
    bool smoothOneStep_ = true;
    float outlineBias_ = 0.004f;
    float stripInset_ = 0.25f;
    float pathBias_ = 0.01f;

    QSet<int> selectedCells_;

    std::vector<TreePlacement> treePlacements_;
    QSet<int> treeOccupiedCells_;
    std::vector<float> selectionOutlineVertices_;
    bool selectionOutlineDirty_ = true;

    QVector3D cameraPos_{ 0, 0, 5 };      // Ð¢ÐµÐºÑƒÑ‰Ð°Ñ Ð¿Ð¾Ð·Ð¸Ñ†Ð¸Ñ ÐºÐ°Ð¼ÐµÑ€Ñ‹ (Ð½Ð°Ñ‡Ð°Ð»ÑŒÐ½Ð¾Ðµ Ð·Ð½Ð°Ñ‡ÐµÐ½Ð¸Ðµ)
    QVector3D lastCameraPos_{ 0, 0, 5 };  // ÐŸÐ¾Ð·Ð¸Ñ†Ð¸Ñ Ð½Ð° Ð¿Ñ€Ð¾ÑˆÐ»Ð¾Ð¼ ÐºÐ°Ð´Ñ€Ðµ Ð´Ð»Ñ Ð´ÐµÑ‚ÐµÐºÑ‚Ð° Ð´Ð²Ð¸Ð¶ÐµÐ½Ð¸Ñ
    mutable std::vector<CachedTriangle> triangleCache_;
    mutable bool cacheValid_ = false;
    mutable QVector3D lastCacheCameraPos_;

    void rebuildCache() const;
    void validateCache() const;

    // ÐÐ¾Ð²Ñ‹Ðµ Ð¿Ð¾Ð»Ñ Ð´Ð»Ñ Ð´ÐµÑ‚ÐµÐºÑ‚Ð¾Ñ€Ð° ÑÐºÐ¾Ñ€Ð¾ÑÑ‚Ð¸
    mutable QVector3D velocity_;
    mutable QElapsedTimer speedTimer_;
    mutable bool speedTimerStarted_ = false;

    mutable QElapsedTimer lastUpdateTimer_;
    mutable bool lastUpdateStarted_ = false;

    VisibilityConfig visibilityConfig_;
};

