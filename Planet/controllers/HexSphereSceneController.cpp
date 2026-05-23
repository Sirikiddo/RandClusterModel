#include "controllers/HexSphereSceneController.h"

#include <QRandomGenerator>
#include <random>
#include <cmath>

#include "generation/MeshGenerators/WireMeshGenerator.h"
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"
#include <QVector3D>

HexSphereSceneController::HexSphereSceneController(SceneViewMode viewMode)
    : viewMode_(viewMode)
    , generator_(createTerrainGeneratorByIndex(generatorIndex_)) {
    rebuildModel();
}

void HexSphereSceneController::setGeneratorByIndex(int idx) {
    generatorIndex_ = normalizeTerrainGeneratorIndex(idx);
    generator_ = createTerrainGeneratorByIndex(generatorIndex_);
}

void HexSphereSceneController::setGenParams(const TerrainParams& params) {
    genParams_ = params;
}

void HexSphereSceneController::setSubdivisionLevel(int level) {
    if (L_ == level) {
        return;
    }
    L_ = level;
    heightStep_ = TerrainMeshPolicy::heightStepForSubdivision(L_);
    rebuildModel();
}

void HexSphereSceneController::stageSubdivisionLevel(int level) {
    if (L_ == level) {
        return;
    }
    L_ = level;
    heightStep_ = TerrainMeshPolicy::heightStepForSubdivision(L_);
    topologyDirty_ = true;
}

void HexSphereSceneController::rebuildTerrainFromInputs() {
    if (isContributorMode()) {
        rebuildContributorScene();
        return;
    }

    rebuildTopology();
    generator_->generate(model_, genParams_);
    updateTerrainMesh();
    generateTreePlacements();
}

void HexSphereSceneController::regenerateTreePlacements() {
    if (isContributorMode()) {
        rebuildContributorScene();
        return;
    }

    generateTreePlacements();
}

void HexSphereSceneController::setSmoothOneStep(bool on) {
    if (smoothOneStep_ == on) {
        return;
    }
    smoothOneStep_ = on;
}

void HexSphereSceneController::setStripInset(float value) {
    stripInset_ = value;
}

void HexSphereSceneController::setOutlineBias(float value) {
    outlineBias_ = value;
}

void HexSphereSceneController::rebuildModel() {
    if (isContributorMode()) {
        rebuildContributorScene();
        return;
    }

    rebuildTopology();
    generator_->generate(model_, genParams_);
    updateTerrainMesh();
    generateTreePlacements();
}

void HexSphereSceneController::regenerateTerrain() {
    if (isContributorMode()) {
        rebuildContributorScene();
        return;
    }

    generator_->generate(model_, genParams_);
    updateTerrainMesh();
    generateTreePlacements();
}

void HexSphereSceneController::clearForShutdown() {
    selectedCells_.clear();
    treePlacements_.clear();
    treeOccupiedCells_.clear();
    terrainCPU_ = TerrainMesh{};
    model_ = HexSphereModel{};
    ico_ = IcoMesh{};
}

void HexSphereSceneController::clearSelection() {
    selectedCells_.clear();
}

void HexSphereSceneController::toggleCellSelection(int cellId) {
    if (selectedCells_.contains(cellId)) {
        selectedCells_.remove(cellId);
    }
    else {
        selectedCells_.insert(cellId);
    }
}

std::optional<std::vector<QVector3D>> HexSphereSceneController::buildPathPolyline() const {
    if (selectedCells_.size() != 2) {
        return std::nullopt;
    }

    const QList<int> list = selectedCells_.values();
    PathBuilder pb(model_, smoothOneStep_ ? 1 : 0);
    pb.build();
    const auto path = pb.astar(list[0], list[1]);
    if (path.empty()) {
        return std::nullopt;
    }

    return pb.polylineOnSphere(path, 8, pathBias_, heightStep_);
}

std::vector<float> HexSphereSceneController::buildWireVertices() const {
    if (isContributorMode()) {
        return {};
    }
    return WireMeshGenerator::buildWireVertices(model_);
}

std::vector<float> HexSphereSceneController::buildSelectionOutlineVertices() const {
    return buildOutlineVerticesForCells(selectedCells_);
}

std::vector<float> HexSphereSceneController::buildOutlineVerticesForCells(const QSet<int>& cells) const {
    if (cells.empty()) {
        return {};
    }
    return SelectionOutlineGenerator::buildSelectionOutlineVertices(
        model_,
        cells,
        heightStep_,
        outlineBias_,
        smoothOneStep_);
}

WaterGeometryData HexSphereSceneController::buildWaterGeometry() const {
    if (isContributorMode()) {
        return {};
    }
    return WaterMeshGenerator::buildWaterGeometry(model_);
}

TerrainSnapshot HexSphereSceneController::captureTerrainSnapshot() const {
    TerrainSnapshot snapshot;
    snapshot.subdivisionLevel = L_;
    snapshot.generatorIndex = generatorIndex_;
    snapshot.params = genParams_;
    snapshot.cells.reserve(model_.cells().size());

    for (const auto& cell : model_.cells()) {
        TerrainCellSnapshot entry;
        entry.height = cell.height;
        entry.biome = cell.biome;
        entry.temperature = cell.temperature;
        entry.humidity = cell.humidity;
        entry.pressure = cell.pressure;
        entry.oreDensity = cell.oreDensity;
        entry.oreType = cell.oreType;
        entry.oreVisual = cell.oreVisual;
        entry.oreNoiseOffset = cell.oreNoiseOffset;
        snapshot.cells.push_back(entry);
    }

    return snapshot;
}

void HexSphereSceneController::applyTerrainSnapshot(const TerrainSnapshot& snapshot) {
    generatorIndex_ = normalizeTerrainGeneratorIndex(snapshot.generatorIndex);
    generator_ = createTerrainGeneratorByIndex(generatorIndex_);
    genParams_ = snapshot.params;
    stageSubdivisionLevel(snapshot.subdivisionLevel);
    rebuildTopology();

    auto& cells = model_.cells();
    const size_t count = std::min(cells.size(), snapshot.cells.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& source = snapshot.cells[i];
        auto& target = cells[i];
        target.height = source.height;
        target.biome = source.biome;
        target.temperature = source.temperature;
        target.humidity = source.humidity;
        target.pressure = source.pressure;
        target.oreDensity = source.oreDensity;
        target.oreType = source.oreType;
        target.oreVisual = source.oreVisual;
        target.oreNoiseOffset = source.oreNoiseOffset;
    }

    updateTerrainMesh();
    generateTreePlacements();
}

float HexSphereSceneController::cellSize() const {
    return std::pow(0.5f, static_cast<float>(L_));
}

bool HexSphereSceneController::isCellOccupiedByTree(int cellId) const {
    return treeOccupiedCells_.contains(cellId);
}

void HexSphereSceneController::generateTreePlacements() {
    treePlacements_.clear();

    const auto& cells = model_.cells();
    if (cells.empty()) {
        treeOccupiedCells_.clear();
        return;
    }

    std::mt19937 rng(static_cast<uint32_t>(genParams_.seed));
    std::uniform_real_distribution<float> randomZeroOne(0.0f, 1.0f);
    std::uniform_real_distribution<float> randomRotation(0.0f, 2.0f * 3.1415926535f);
    std::uniform_real_distribution<float> randomScale(0.85f, 1.2f);

    for (const Cell& cell : cells) {
        if (cell.height <= genParams_.seaLevel) {
            continue;
        }

        TreeType treeType = TreeType::Oak;
        float spawnChance = 0.0f;
        QVector3D foliageColor(0.2f, 0.6f, 0.25f);
        QVector3D trunkColor(0.42f, 0.28f, 0.14f);

        switch (cell.biome) {
        case Biome::Grass:
            treeType = TreeType::Oak;
            spawnChance = 0.18f;
            foliageColor = QVector3D(0.28f, 0.62f, 0.24f);
            break;
        case Biome::Jungle:
            treeType = TreeType::Oak;
            spawnChance = 0.33f;
            foliageColor = QVector3D(0.12f, 0.52f, 0.18f);
            break;
        case Biome::Savanna:
            treeType = TreeType::Oak;
            spawnChance = 0.10f;
            foliageColor = QVector3D(0.54f, 0.64f, 0.23f);
            break;
        case Biome::Snow:
        case Biome::Tundra:
            treeType = TreeType::Fir;
            spawnChance = 0.16f;
            foliageColor = QVector3D(0.18f, 0.36f, 0.24f);
            break;
        default:
            break;
        }

        if (spawnChance <= 0.0f || randomZeroOne(rng) > spawnChance) {
            continue;
        }

        TreePlacement placement;
        placement.cellId = cell.id;
        placement.rotation = randomRotation(rng);
        placement.scale = randomScale(rng);
        placement.treeType = treeType;
        placement.foliageColor = foliageColor;
        placement.trunkColor = trunkColor;
        treePlacements_.push_back(placement);
    }

    updateTreeOccupiedCells();
}

void HexSphereSceneController::rebuildTopology() {
    if (!topologyDirty_ && model_.cellCount() > 0) {
        return;
    }

    ico_ = icoBuilder_.build(L_);
    model_.rebuildFromIcosphere(ico_);
    topologyDirty_ = false;
}

void HexSphereSceneController::updateTerrainMesh() {
    if (isContributorMode()) {
        terrainCPU_ = TerrainMesh{};
        return;
    }

    const TerrainRenderConfig renderConfig = terrainRenderConfig();
    const TerrainMeshOptions options = TerrainMeshPolicy::buildOptions(L_, renderConfig);
    heightStep_ = options.heightStep;
    terrainCPU_ = TerrainMeshGenerator::buildTerrainMesh(model_, options);
}

void HexSphereSceneController::updateTreeOccupiedCells() {
    treeOccupiedCells_.clear();
    for (const auto& placement : treePlacements_) {
        treeOccupiedCells_.insert(placement.cellId);
    }
}

void HexSphereSceneController::rebuildContributorScene() {
    selectedCells_.clear();
    heightStep_ = TerrainMeshPolicy::heightStepForSubdivision(L_);

    Cell contributorCell;
    contributorCell.id = 0;
    contributorCell.centroid = QVector3D(0.0f, 1.0f, 0.0f);
    contributorCell.height = 0;
    contributorCell.biome = Biome::Grass;

    model_ = HexSphereModel{};
    model_.debug_setCellsAndDual({ contributorCell }, {});
    terrainCPU_ = TerrainMesh{};
    generateTreePlacements();
}
