#pragma once

#include <QSet>
#include <QVector3D>
#include <optional>
#include <memory>
#include <vector>

#include "HexSphereModel.h"
#include "TerrainGenerator.h"
#include "TerrainTessellator.h"
#include "PathBuilder.h"

// Данные для генерации воды перед загрузкой в GPU
struct WaterGeometryData {
    std::vector<float> positions;
    std::vector<float> edgeFlags;
    std::vector<uint32_t> indices;
};

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

    std::optional<std::vector<QVector3D>> buildPathPolyline();

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
};
