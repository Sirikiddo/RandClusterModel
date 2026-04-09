#include "controllers/HexSphereSceneController.h"

#include <QtGlobal>
#include <algorithm>
#include <random>
#include <cmath>

// �?обавляем н�?жн�?е include
#include "generation/MeshGenerators/WireMeshGenerator.h"
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"
#include <QVector3D>
#include <QElapsedTimer>

namespace {
    constexpr float kContributorTreeScale = 0.5f;
}

HexSphereSceneController::HexSphereSceneController(SceneViewMode viewMode)
    : viewMode_(viewMode)
    , generator_(createTerrainGeneratorByIndex(generatorIndex_)) {
    genParams_ = TerrainParams{ /*seed=*/12345u, /*seaLevel=*/3, /*scale=*/3.0f };
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
    stageSubdivisionLevel(level);
    rebuildTerrainFromInputs();
}

void HexSphereSceneController::stageSubdivisionLevel(int level) {
    if (L_ == level) {
        return;
    }
    L_ = level;
    heightStep_ = autoHeightStep();
    topologyDirty_ = true;
}

void HexSphereSceneController::rebuildTerrainFromInputs() {
    if (topologyDirty_) {
        rebuildModel();
        return;
    }
    regenerateTerrain();
}

void HexSphereSceneController::setSmoothOneStep(bool on) {
    smoothOneStep_ = on;
}

void HexSphereSceneController::setStripInset(float value) {
    stripInset_ = std::clamp(value, 0.0f, 0.49f);
}

void HexSphereSceneController::setOutlineBias(float value) {
    outlineBias_ = std::max(0.0f, value);
}

void HexSphereSceneController::rebuildTopology() {
    ico_ = icoBuilder_.build(L_);
    model_.rebuildFromIcosphere(ico_);
}

void HexSphereSceneController::rebuildModel() {
    if (isContributorMode()) {
        rebuildContributorScene();
        topologyDirty_ = false;
        return;
    }

    rebuildTopology();
    topologyDirty_ = false;
    regenerateTerrain();
}

void HexSphereSceneController::regenerateTerrain() {
    if (isContributorMode()) {
        rebuildContributorScene();
        return;
    }

    if (generator_) {
        generator_->generate(model_, genParams_);
    }
    updateTerrainMesh();
    generateTreePlacements();
}

void HexSphereSceneController::clearForShutdown() {
    selectedCells_.clear();
    treePlacements_.clear();
    treeOccupiedCells_.clear();
    triangleCache_.clear();
    cacheValid_ = false;
    velocity_ = QVector3D();
    cameraPos_ = QVector3D();
    lastCameraPos_ = QVector3D();
    terrainCPU_ = TerrainMesh{};
    model_ = HexSphereModel{};
    ico_ = IcoMesh{};
    generator_.reset();
}

void HexSphereSceneController::clearSelection() {
    if (isContributorMode()) {
        return;
    }
    selectedCells_.clear();
}

void HexSphereSceneController::toggleCellSelection(int cellId) {
    if (isContributorMode()) {
        return;
    }
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

    auto it = selectedCells_.begin();
    const int a = *it;
    ++it;
    const int b = *it;

    PathBuilder pb(model_, smoothOneStep_ ? 1 : 0);
    pb.build();
    auto ids = pb.astar(a, b);
    auto poly = pb.polylineOnSphere(ids, /*segmentsPerEdge=*/8, pathBias_, heightStep_);
    return poly;
}

std::vector<float> HexSphereSceneController::buildWireVertices() const {
    if (isContributorMode()) {
        return {};
    }
    // WireMeshGenerator ожидае�? const HexSphereModel&
    return WireMeshGenerator::buildWireVertices(model_);
}

std::vector<float> HexSphereSceneController::buildSelectionOutlineVertices() const {
    if (isContributorMode()) {
        return {};
    }
    // SelectionOutlineGenerator ожидае�?: const HexSphereModel&, const QSet<int>&, float, float, bool
    return SelectionOutlineGenerator::buildSelectionOutlineVertices(
        model_, selectedCells_, heightStep_, outlineBias_, smoothOneStep_);
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
        TerrainCellSnapshot cellSnapshot;
        cellSnapshot.height = cell.height;
        cellSnapshot.biome = cell.biome;
        cellSnapshot.temperature = cell.temperature;
        cellSnapshot.humidity = cell.humidity;
        cellSnapshot.pressure = cell.pressure;
        cellSnapshot.oreDensity = cell.oreDensity;
        cellSnapshot.oreType = cell.oreType;
        cellSnapshot.oreVisual = cell.oreVisual;
        cellSnapshot.oreNoiseOffset = cell.oreNoiseOffset;
        snapshot.cells.push_back(cellSnapshot);
    }

    return snapshot;
}

void HexSphereSceneController::applyTerrainSnapshot(const TerrainSnapshot& snapshot) {
    if (isContributorMode()) {
        return;
    }

    generatorIndex_ = normalizeTerrainGeneratorIndex(snapshot.generatorIndex);
    generator_ = createTerrainGeneratorByIndex(generatorIndex_);
    genParams_ = snapshot.params;
    L_ = snapshot.subdivisionLevel;
    topologyDirty_ = false;

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

    selectedCells_.clear();
    updateTerrainMesh();
    generateTreePlacements();
}

float HexSphereSceneController::autoHeightStep() const {
    const float baseStep = 0.05f;
    const float reductionFactor = 0.4f;
    return baseStep / (1.0f + L_ * reductionFactor);
}

void HexSphereSceneController::updateTerrainMesh() {
    if (isContributorMode()) {
        terrainCPU_ = TerrainMesh{};
        cacheValid_ = false;
        triangleCache_.clear();
        return;
    }

    heightStep_ = autoHeightStep();
    TerrainMeshOptions options;
    options.heightStep = heightStep_;
    options.inset = stripInset_;
    options.smoothOneStep = smoothOneStep_;
    options.outerTrim = 0.15f;
    options.doCaps = true;
    options.doBlades = true;
    options.doCornerTris = true;
    options.doEdgeCliffs = true;

    terrainCPU_ = TerrainMeshGenerator::buildTerrainMesh(model_, options);
}

float HexSphereSceneController::cellSize() const {
    const float baseForL2 = 1.0f;
    const float factor = 0.7f;

    if (L_ == 2) return baseForL2;
    else if (L_ < 2) return baseForL2 * std::pow(1.0f / factor, 2 - L_);
    else return baseForL2 * std::pow(factor, L_ - 2);
}

bool HexSphereSceneController::isCellOccupiedByTree(int cellId) const {
    return std::any_of(treePlacements_.begin(), treePlacements_.end(),
        [cellId](const TreePlacement& p) { return p.cellId == cellId; });
}

void HexSphereSceneController::updateTreeOccupiedCells() {
    treeOccupiedCells_.clear();
    for (const auto& placement : treePlacements_) {
        treeOccupiedCells_.insert(placement.cellId);
    }
}

void HexSphereSceneController::generateTreePlacements() {
    treePlacements_.clear();

    if (isContributorMode()) {
        TreePlacement placement;
        placement.cellId = 0;
        placement.treeType = TreeType::Oak;
        placement.placementMode = TreePlacement::PlacementMode::World;
        placement.worldPosition = QVector3D(0.0f, 1.0f, 0.0f);
        placement.worldUp = QVector3D(0.0f, 1.0f, 0.0f);
        placement.worldYaw = 0.0f;
        placement.worldScale = kContributorTreeScale;
        placement.scale = 1.0f;
        treePlacements_.push_back(placement);
        updateTreeOccupiedCells();
        return;
    }

    const auto& cells = model_.cells();

    const uint32_t deterministicSeed =
        genParams_.seed ^
        (static_cast<uint32_t>(generatorIndex_ + 1) * 0x9e3779b9u) ^
        (static_cast<uint32_t>(L_ + 1) * 0x85ebca6bu);
    std::mt19937 gen(deterministicSeed);
    std::uniform_real_distribution<float> distBary(0.1f, 0.8f);
    std::uniform_real_distribution<float> distScale(0.7f, 1.3f);
    std::uniform_real_distribution<float> distRot(0.0f, 2.0f * 3.14159f);

    // Зеленые оттенки
    std::uniform_real_distribution<float> distGreenR(0.15f, 0.45f);
    std::uniform_real_distribution<float> distGreenG(0.55f, 0.85f);
    std::uniform_real_distribution<float> distGreenB(0.1f, 0.35f);

    // Зеленые оттенки для ёлочек (более темные, синеватые)
    std::uniform_real_distribution<float> distFirR(0.1f, 0.35f);
    std::uniform_real_distribution<float> distFirG(0.35f, 0.65f);
    std::uniform_real_distribution<float> distFirB(0.2f, 0.45f);

    // Оранжевые оттенки
    std::uniform_real_distribution<float> distAutumnR(0.7f, 1.0f);
    std::uniform_real_distribution<float> distAutumnG(0.4f, 0.7f);
    std::uniform_real_distribution<float> distAutumnB(0.1f, 0.3f);

    // Ствол
    std::uniform_real_distribution<float> distTrunkR(0.4f, 0.65f);
    std::uniform_real_distribution<float> distTrunkG(0.25f, 0.4f);
    std::uniform_real_distribution<float> distTrunkB(0.1f, 0.2f);

    // Ствол для ёлочек
    std::uniform_real_distribution<float> distFirTrunkR(0.35f, 0.55f);
    std::uniform_real_distribution<float> distFirTrunkG(0.2f, 0.35f);
    std::uniform_real_distribution<float> distFirTrunkB(0.1f, 0.18f);

    int greenCount = 0;
    int firCount = 0;
    int autumnCount = 0;

    for (size_t i = 0; i < cells.size(); ++i) {
        const auto& cell = cells[i];

        bool shouldPlaceTree = false;
        TreeType treeTypeToPlace = TreeType::Oak;

        if (cell.biome == Biome::Grass) {
            shouldPlaceTree = true;
            // 70% обычные деревья, 30% ёлочки
            std::uniform_real_distribution<float> distTreeType(0.0f, 1.0f);
            if (distTreeType(gen) < 0.3f) {
                treeTypeToPlace = TreeType::Fir;
            }
            else {
                treeTypeToPlace = TreeType::Oak;
            }
        }
        else if (cell.biome == Biome::Savanna) {
            shouldPlaceTree = true;
            treeTypeToPlace = TreeType::Oak;
        }
        else if (cell.biome == Biome::Snow) {
            std::uniform_real_distribution<float> distSnowTree(0.0f, 1.0f);
            if (distSnowTree(gen) < 0.4f) {
                shouldPlaceTree = true;
                treeTypeToPlace = TreeType::Fir;
            }
        }
        else if (cell.biome == Biome::Tundra) {
            std::uniform_real_distribution<float> distTundraTree(0.0f, 1.0f);
            if (distTundraTree(gen) < 0.25f) {
                shouldPlaceTree = true;
                treeTypeToPlace = TreeType::Fir;
            }
        }

        if (!shouldPlaceTree) continue;

        TreePlacement placement;
        placement.cellId = static_cast<int>(i);
        placement.treeType = treeTypeToPlace;

        if (!cell.poly.empty()) {
            std::uniform_int_distribution<int> distTri(0, cell.poly.size() - 1);
            placement.triangleIdx = distTri(gen);
        }

        float u = distBary(gen);
        float v = distBary(gen);
        if (u + v > 1.0f) {
            u = 1.0f - u;
            v = 1.0f - v;
        }
        placement.baryU = u;
        placement.baryV = v;
        placement.baryW = 1.0f - u - v;

        if (cell.biome == Biome::Savanna) {
            // Осенние деревья
            placement.colorType = TreePlacement::TreeColorType::Autumn;
            placement.isYellowCellTree = true;
            autumnCount++;

            placement.foliageColor = QVector3D(
                distAutumnR(gen),
                distAutumnG(gen),
                distAutumnB(gen)
            );

            placement.trunkColor = QVector3D(
                distTrunkR(gen) * 0.7f,
                distTrunkG(gen) * 0.6f,
                distTrunkB(gen) * 0.5f
            );

            placement.scale = distScale(gen) * 0.85f;
        }
        else if (placement.treeType == TreeType::Fir) {
            // Ёлочки
            placement.colorType = TreePlacement::TreeColorType::Green;
            placement.isYellowCellTree = false;
            firCount++;

            placement.foliageColor = QVector3D(
                distFirR(gen),
                distFirG(gen),
                distFirB(gen)
            );

            placement.trunkColor = QVector3D(
                distFirTrunkR(gen),
                distFirTrunkG(gen),
                distFirTrunkB(gen)
            );

            placement.scale = distScale(gen) * 0.9f;
        }
        else {
            // Зеленые деревья
            placement.colorType = TreePlacement::TreeColorType::Green;
            placement.isYellowCellTree = false;
            greenCount++;

            placement.foliageColor = QVector3D(
                distGreenR(gen),
                distGreenG(gen),
                distGreenB(gen)
            );

            placement.trunkColor = QVector3D(
                distTrunkR(gen),
                distTrunkG(gen),
                distTrunkB(gen)
            );

            if (cell.humidity > 0.7f) {
                placement.scale = distScale(gen) * 1.2f;
            }
            else if (cell.humidity < 0.3f) {
                placement.scale = distScale(gen) * 0.7f;
            }
            else {
                placement.scale = distScale(gen);
            }
        }

        placement.rotation = distRot(gen);
        treePlacements_.push_back(placement);
    }

    qDebug() << "Generated" << treePlacements_.size() << "tree placements";
    qDebug() << "  - Green trees:" << greenCount;
    qDebug() << "  - Fir trees:" << firCount;
    qDebug() << "  - Autumn trees:" << autumnCount;
    updateTreeOccupiedCells();
}

void HexSphereSceneController::regenerateTreePlacements() {
    generateTreePlacements();
}

void HexSphereSceneController::rebuildContributorScene() {
    selectedCells_.clear();
    heightStep_ = autoHeightStep();

    Cell contributorCell;
    contributorCell.id = 0;
    contributorCell.height = -35;
    contributorCell.biome = Biome::Grass;
    contributorCell.centroid = QVector3D(0.0f, 1.0f, 0.0f);
    contributorCell.temperature = 0.5f;
    contributorCell.humidity = 0.5f;
    contributorCell.pressure = 0.5f;

    model_ = HexSphereModel{};
    model_.debug_setCellsAndDual({ contributorCell }, {});
    terrainCPU_ = TerrainMesh{};
    triangleCache_.clear();
    cacheValid_ = false;
    generateTreePlacements();
}

static std::vector<QVector3D> convertToQVector3D(const std::vector<float>& positions) {
    std::vector<QVector3D> result;
    result.reserve(positions.size() / 3);
    for (size_t i = 0; i < positions.size(); i += 3) {
        result.emplace_back(positions[i], positions[i + 1], positions[i + 2]);
    }
    return result;
}

std::vector<uint32_t> HexSphereSceneController::getVisibleIndices(const QVector3D& cameraPos) const {
    validateCache();

    QVector3D planetCenter(0, 0, 0);
    QVector3D toCam = (cameraPos - planetCenter).normalized();

    std::vector<uint32_t> visibleIndices;
    visibleIndices.reserve(terrainCPU_.idx.size() / 2);

    for (const auto& tri : triangleCache_) {
        QVector3D normal = tri.center.normalized();
        if (QVector3D::dotProduct(normal, toCam) > 0.0f) {
            visibleIndices.push_back(tri.i0);
            visibleIndices.push_back(tri.i1);
            visibleIndices.push_back(tri.i2);
        }
    }

    return visibleIndices;
}

TerrainMesh HexSphereSceneController::getVisibleTerrainMesh() const {
    TerrainMesh visibleMesh = terrainCPU_;
    visibleMesh.idx = getVisibleIndices(cameraPos_);
    return visibleMesh;
}

void HexSphereSceneController::updateVisibility(const QVector3D& cameraPos) {
    setCameraPosition(cameraPos);
}

std::pair<size_t, size_t> HexSphereSceneController::getVisibilityStats() const {
    size_t totalTriangles = terrainCPU_.idx.size() / 3;
    size_t visibleTriangles = getVisibleIndices(cameraPos_).size() / 3;
    return { visibleTriangles, totalTriangles };
}

void HexSphereSceneController::rebuildCache() const {
    if (terrainCPU_.idx.empty() || terrainCPU_.pos.empty()) {
        triangleCache_.clear();
        cacheValid_ = false;
        return;
    }

    std::vector<QVector3D> positions = convertToQVector3D(terrainCPU_.pos);
    triangleCache_.clear();
    triangleCache_.reserve(terrainCPU_.idx.size() / 3);

    QElapsedTimer timer;
    timer.start();

    for (size_t i = 0; i + 2 < terrainCPU_.idx.size(); i += 3) {
        uint32_t i0 = terrainCPU_.idx[i];
        uint32_t i1 = terrainCPU_.idx[i + 1];
        uint32_t i2 = terrainCPU_.idx[i + 2];
        QVector3D center = (positions[i0] + positions[i1] + positions[i2]) * (1.0f / 3.0f);
        triangleCache_.push_back({ center, i0, i1, i2, 0.0f });
    }

    qDebug() << "Cache rebuilt:" << triangleCache_.size() << "triangles in" << timer.elapsed() << "ms";
    cacheValid_ = true;
}

void HexSphereSceneController::validateCache() const {
    if (!cacheValid_ || triangleCache_.size() != terrainCPU_.idx.size() / 3) {
        rebuildCache();
    }
}

