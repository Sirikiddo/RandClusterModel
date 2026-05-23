#pragma once

#include <QSet>
#include <QtDebug>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include "core/AppViewConfig.h"
#include "dag/TerrainBackendTypes.h"
#include "controllers/PathBuilder.h"
#include "generation/MeshGenerators/TerrainMeshPolicy.h"
#include "generation/MeshGenerators/WaterMeshGenerator.h"
#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

class HexSphereSceneController {
public:
    explicit HexSphereSceneController(SceneViewMode viewMode = SceneViewMode::Planet);

    void setGeneratorByIndex(int idx);
    void setGenParams(const TerrainParams& params);
    void setSubdivisionLevel(int level);
    void stageSubdivisionLevel(int level);
    void rebuildTerrainFromInputs();
    void regenerateTreePlacements();

    void setSmoothOneStep(bool on);
    void setStripInset(float value);
    void setOutlineBias(float value);

    void rebuildModel();
    void regenerateTerrain();
    void clearForShutdown();

    void clearSelection();
    void toggleCellSelection(int cellId);

    std::optional<std::vector<QVector3D>> buildPathPolyline() const;

    std::vector<float> buildWireVertices() const;
    std::vector<float> buildSelectionOutlineVertices() const;
    WaterGeometryData buildWaterGeometry() const;
    TerrainSnapshot captureTerrainSnapshot() const;
    void applyTerrainSnapshot(const TerrainSnapshot& snapshot);

    const HexSphereModel& model() const { return model_; }
    HexSphereModel& modelMutable() { return model_; }
    const TerrainMesh& terrain() const { return terrainCPU_; }
    const QSet<int>& selectedCells() const { return selectedCells_; }

    int subdivisionLevel() const { return L_; }
    int generatorIndex() const { return generatorIndex_; }
    float heightStep() const { return heightStep_; }
    float outlineBias() const { return outlineBias_; }
    float stripInset() const { return stripInset_; }
    bool smoothOneStep() const { return smoothOneStep_; }
    float pathBias() const { return pathBias_; }
    TerrainRenderConfig terrainRenderConfig() const { return TerrainRenderConfig{ smoothOneStep_, stripInset_ }; }

    float cellSize() const;
    bool isCellOccupiedByTree(int cellId) const;
    const std::vector<TreePlacement>& getTreePlacements() const { return treePlacements_; }
    void generateTreePlacements();
    SceneViewMode sceneViewMode() const { return viewMode_; }
    bool isContributorMode() const { return viewMode_ == SceneViewMode::Contributor; }

private:
    void rebuildTopology();
    void updateTerrainMesh();
    void rebuildContributorScene();

    void updateTreeOccupiedCells();

    SceneViewMode viewMode_ = SceneViewMode::Planet;

    IcosphereBuilder icoBuilder_;
    IcoMesh ico_;
    HexSphereModel model_;
    TerrainMesh terrainCPU_;

    std::unique_ptr<ITerrainGenerator> generator_;
    TerrainParams genParams_{};
    int generatorIndex_ = 3;

    int L_ = 2;
    bool topologyDirty_ = false;
    float heightStep_ = 0.06f;
    bool smoothOneStep_ = true;
    float outlineBias_ = 0.004f;
    float stripInset_ = 0.25f;
    float pathBias_ = 0.01f;

    QSet<int> selectedCells_;

    std::vector<TreePlacement> treePlacements_;
    QSet<int> treeOccupiedCells_;
};

