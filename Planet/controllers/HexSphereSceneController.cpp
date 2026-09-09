#include "controllers/HexSphereSceneController.h"

#include <QtGlobal>
#include <algorithm>
#include <random>
#include <cmath>
#include <utility>

// �?обавляем н�?жн�?е include
#include "generation/MeshGenerators/WireMeshGenerator.h"
#include "generation/OreGenerator.h"
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"
#include <QVector3D>
#include <QElapsedTimer>

namespace {
    constexpr float kContributorTreeScale = 0.5f;

    bool waveInputsEqual(const WaterParams& lhs, const WaterParams& rhs) {
        return lhs.preset == rhs.preset
            && lhs.waveStrength == rhs.waveStrength
            && lhs.shellWaveAmplitude == rhs.shellWaveAmplitude
            && lhs.shellWaveFrequency == rhs.shellWaveFrequency
            && lhs.shellWaveSpeed == rhs.shellWaveSpeed
            && lhs.octaveDetail == rhs.octaveDetail;
    }

    bool waterParamsEqual(const WaterParams& lhs, const WaterParams& rhs) {
        return waveInputsEqual(lhs, rhs)
            && lhs.beachWidth == rhs.beachWidth
            && lhs.fresnelStrength == rhs.fresnelStrength
            && lhs.specularIntensity == rhs.specularIntensity
            && lhs.glintIntensity == rhs.glintIntensity
            && lhs.glintThreshold == rhs.glintThreshold
            && lhs.glintSharpness == rhs.glintSharpness
            && lhs.foamIntensity == rhs.foamIntensity
            && lhs.roughness == rhs.roughness
            && lhs.reflectionStrength == rhs.reflectionStrength
            && lhs.opacity == rhs.opacity
            && lhs.depthOpticalDensity == rhs.depthOpticalDensity
            && lhs.depthAlphaDensity == rhs.depthAlphaDensity
            && lhs.shallowColor == rhs.shallowColor
            && lhs.deepColor == rhs.deepColor
            && lhs.wetSandColor == rhs.wetSandColor
            && lhs.drySandColor == rhs.drySandColor;
    }
}

HexSphereSceneController::HexSphereSceneController(SceneViewMode viewMode)
    : viewMode_(viewMode)
    , generator_(createTerrainGeneratorByIndex(kDefaultTerrainGeneratorIndex)) {
    rebuildModel();
}

void HexSphereSceneController::setGeneratorByIndex(int idx) {
    generatorIndex_ = normalizeTerrainGeneratorIndex(idx);
    generator_ = createTerrainGeneratorByIndex(generatorIndex_);
}

void HexSphereSceneController::setGenParams(const TerrainParams& params) {
    genParams_ = params;
}

WaterUpdateKind HexSphereSceneController::setWaterParams(const WaterParams& params) {
    if (waterParamsEqual(waterParams_, params)) {
        return WaterUpdateKind::None;
    }
    const bool coastChanged = waterParams_.beachWidth != params.beachWidth;
    const bool waveChanged = !waveInputsEqual(waterParams_, params);
    waterParams_ = params;
    if (coastChanged) {
        updateTerrainMesh();
        return WaterUpdateKind::CoastGeometry;
    }
    if (waveChanged) {
        refreshResolvedWaterState();
        return WaterUpdateKind::WaveSpec;
    }
    refreshResolvedWaterParams();
    return WaterUpdateKind::OpticsOnly;
}

void HexSphereSceneController::setSubdivisionLevel(int level) {
    if (L_ == level) {
        return;
    }
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
    selectionOutlineDirty_ = true;
}

void HexSphereSceneController::setStripInset(float value) {
    stripInset_ = std::clamp(value, 0.0f, 0.49f);
}

void HexSphereSceneController::setOutlineBias(float value) {
    outlineBias_ = std::max(0.0f, value);
    selectionOutlineDirty_ = true;
}

void HexSphereSceneController::rebuildTopology() {
    ico_ = icoBuilder_.build(L_);
    model_.rebuildFromIcosphere(ico_);
    model_.setBaseRadius(HexSphereModel::kDefaultBaseRadius);
    model_.setHeightStep(autoHeightStep());
    model_.setWaterSurfaceLevel(HexSphereModel::kDefaultWaterSurfaceLevel);
    rebuildWaterProxy();
}

void HexSphereSceneController::rebuildWaterProxy() {
    waterCPU_ = WaterMeshGenerator::buildWaterGeometry(model_);
    ++waterProxyRevision_;
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
        generateCanonicalTerrain(*generator_, model_, genParams_);
        if (generatorIndex_ == 0) {
            OreGenerator::clear(model_);
        }
        else {
            OreGenerator::generate(model_, genParams_.seed);
        }
    }
    updateTerrainMesh();
    generateTreePlacements();
}

void HexSphereSceneController::rebuildDerivedGeometry() {
    if (isContributorMode()) {
        rebuildContributorScene();
        return;
    }

    updateTerrainMesh();
    generateTreePlacements();
}

void HexSphereSceneController::clearForShutdown() {
    selectedCells_.clear();
    selectionOutlineVertices_.clear();
    selectionOutlineDirty_ = true;
    treePlacements_.clear();
    treeOccupiedCells_.clear();
    triangleCache_.clear();
    cacheValid_ = false;
    velocity_ = QVector3D();
    cameraPos_ = QVector3D();
    lastCameraPos_ = QVector3D();
    terrainCPU_ = TerrainMesh{};
    waterCPU_ = WaterGeometryData{};
    coastalBand_ = CoastalBandData{};
    model_ = HexSphereModel{};
    ico_ = IcoMesh{};
    generator_.reset();
}

void HexSphereSceneController::clearSelection() {
    if (isContributorMode()) {
        return;
    }
    selectedCells_.clear();
    selectionOutlineDirty_ = true;
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
    selectionOutlineDirty_ = true;
}

void HexSphereSceneController::setSelectionOutlineVertices(std::vector<float> vertices) {
    selectionOutlineVertices_ = std::move(vertices);
    selectionOutlineDirty_ = false;
}

void HexSphereSceneController::setTreePlacements(std::vector<TreePlacement> placements) {
    treePlacements_ = std::move(placements);
    updateTreeOccupiedCells();
}




std::vector<QVector3D> HexSphereSceneController::buildPathPolyline(const std::vector<int>& path) const {
    PathBuilder pb(model_, smoothOneStep_ ? 1 : 0);
    return pb.polylineOnSphere(path, /*segmentsPerEdge=*/8, pathBias_, heightStep_);
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
    if (!selectionOutlineDirty_) {
        return selectionOutlineVertices_;
    }
    // SelectionOutlineGenerator ожидае�?: const HexSphereModel&, const QSet<int>&, float, float, bool
    return SelectionOutlineGenerator::buildSelectionOutlineVertices(
        model_, selectedCells_, heightStep_, outlineBias_, smoothOneStep_);
}

std::vector<float> HexSphereSceneController::buildOutlineVerticesForCells(const QSet<int>& cells) const {
    if (isContributorMode() || cells.empty()) {
        return {};
    }
    return SelectionOutlineGenerator::buildSelectionOutlineVertices(
        model_, cells, heightStep_, outlineBias_, smoothOneStep_);
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
    }

    selectedCells_.clear();
    selectionOutlineVertices_.clear();
    selectionOutlineDirty_ = true;
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
        waterCPU_ = WaterGeometryData{};
        coastalBand_ = CoastalBandData{};
        cacheValid_ = false;
        triangleCache_.clear();
        return;
    }

    heightStep_ = autoHeightStep();
    model_.setHeightStep(heightStep_);
    rebuildCoastalBand();
    buildTerrainMeshFromCurrentCoast();
    refreshResolvedWaterState();
}

void HexSphereSceneController::buildTerrainMeshFromCurrentCoast() {
    TerrainMeshOptions options;
    options.heightStep = heightStep_;
    options.inset = stripInset_;
    options.smoothOneStep = smoothOneStep_;
    options.outerTrim = 0.15f;
    options.doCaps = true;
    options.doBlades = true;
    options.doCornerTris = true;
    options.doEdgeCliffs = true;
    options.coastalBand = &coastalBand_;
    options.waterParams = &waterParams_;

    terrainCPU_ = TerrainMeshGenerator::buildTerrainMesh(model_, options);
    cacheValid_ = false;
    triangleCache_.clear();
    selectionOutlineDirty_ = true;
}

void HexSphereSceneController::rebuildTerrainPresentation() {
    if (isContributorMode()) {
        rebuildContributorScene();
        return;
    }
    buildTerrainMeshFromCurrentCoast();
}

void HexSphereSceneController::refreshResolvedWaterState() {
    refreshResolvedWaterParams();
    const WaterWaveSpec requested = makeWaterWaveSpec(resolvedWaterParams_, model_);
    resolvedWaterWaveSpec_ = resolveWaterWaveSpec(requested, resolvedWaterParams_, model_);
}

void HexSphereSceneController::refreshResolvedWaterParams() {
    resolvedWaterParams_ = ::resolvedWaterParams(waterParams_, &model_);
}

void HexSphereSceneController::rebuildCoastalBand() {
    coastalBand_ = buildCoastalBandData(model_, waterParams_);
}

float HexSphereSceneController::cellSize() const {
    const float baseForL2 = 1.0f;
    const float factor = 0.7f;

    if (L_ == 2) return baseForL2;
    else if (L_ < 2) return baseForL2 * std::pow(1.0f / factor, 2 - L_);
    else return baseForL2 * std::pow(factor, L_ - 2);
}

float HexSphereSceneController::getModelScaleFactor() const {
    // Базовый размер ячейки относительно L=2
    const float baseCellSize = cellSize();

    // Используем степенную функцию для более сильного масштабирования
    const float powerScale = std::pow(baseCellSize, 1.4f);

    // Ограничения, чтобы модели не исчезали и не выходили за пределы
    constexpr float kMinScale = 0.15f;
    constexpr float kMaxScale = 3.5f;
    const float clampedScale = std::clamp(powerScale, kMinScale, kMaxScale);

    return clampedScale;
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
    const float modelScale = getModelScaleFactor();

    const uint32_t deterministicSeed =
        genParams_.seed ^
        (static_cast<uint32_t>(generatorIndex_ + 1) * 0x9e3779b9u) ^
        (static_cast<uint32_t>(L_ + 1) * 0x85ebca6bu);
    std::mt19937 gen(deterministicSeed);
    std::uniform_real_distribution<float> distBary(0.1f, 0.8f);
    std::uniform_real_distribution<float> distScale(0.7f, 1.3f);
    std::uniform_real_distribution<float> distRot(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> distTreePresence(0.0f, 1.0f);

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
            shouldPlaceTree = distTreePresence(gen) < 0.28f;
            if (shouldPlaceTree) {
                // 70% обычные деревья, 30% ёлочки
                std::uniform_real_distribution<float> distTreeType(0.0f, 1.0f);
                if (distTreeType(gen) < 0.3f) {
                    treeTypeToPlace = TreeType::Fir;
                }
                else {
                    treeTypeToPlace = TreeType::Oak;
                }
            }
        }
        else if (cell.biome == Biome::Savanna) {
            shouldPlaceTree = distTreePresence(gen) < 0.16f;
            treeTypeToPlace = TreeType::Oak;
        }
        else if (cell.biome == Biome::Snow) {
            if (distTreePresence(gen) < 0.12f) {
                shouldPlaceTree = true;
                treeTypeToPlace = TreeType::Fir;
            }
        }
        else if (cell.biome == Biome::Tundra) {
            if (distTreePresence(gen) < 0.08f) {
                shouldPlaceTree = true;
                treeTypeToPlace = TreeType::Fir;
            }
        }

        if (!shouldPlaceTree) continue;

        TreePlacement placement;
        placement.cellId = static_cast<int>(i);
        placement.treeType = treeTypeToPlace;
        placement.scale *= modelScale;

        if (!cell.poly.empty()) {
            std::uniform_int_distribution<int> distTri(0, static_cast<int>(cell.poly.size()) - 1);
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


std::vector<float> HexSphereSceneController::buildRoadMesh(const std::vector<int>& path) const {
    std::vector<float> vertices;
    if (path.size() < 2) return vertices;

    // Получаем масштаб
    const float modelScale = getModelScaleFactor();

    // Масштабируем параметры дороги
    const float roadWidth = 0.04f * modelScale;
    const float heightOffset = 0.0000000099f * modelScale;
    const float cornerSegments = 16;
    const float capSegments = 12;  // Количество сегментов для полукруга

    // Получаем точки пути
    auto pathPoints = buildPathPolyline(path);
    if (pathPoints.size() < 2) return vertices;

    // ===== УДЛИНЯЕМ ПЕРВУЮ И ПОСЛЕДНЮЮ ТОЧКИ =====
    std::vector<QVector3D> extendedPoints = pathPoints;

    if (extendedPoints.size() >= 2) {
        QVector3D firstDir = (extendedPoints[1] - extendedPoints[0]).normalized();
        QVector3D firstExtend = extendedPoints[0] - firstDir * (roadWidth * 2.0f);
        extendedPoints.insert(extendedPoints.begin(), firstExtend);
    }

    if (extendedPoints.size() >= 2) {
        size_t lastIdx = extendedPoints.size() - 1;
        QVector3D lastDir = (extendedPoints[lastIdx] - extendedPoints[lastIdx - 1]).normalized();
        QVector3D lastExtend = extendedPoints[lastIdx] + lastDir * (roadWidth * 2.0f);
        extendedPoints.push_back(lastExtend);
    }

    // Структура сегмента
    struct Segment {
        QVector3D start;
        QVector3D end;
        QVector3D dir;
        QVector3D up;
        QVector3D right;
        QVector3D startLeft;
        QVector3D startRight;
        QVector3D endLeft;
        QVector3D endRight;
    };

    std::vector<Segment> segments;
    segments.reserve(extendedPoints.size() - 1);

    for (size_t i = 0; i + 1 < extendedPoints.size(); ++i) {
        Segment seg;
        seg.start = extendedPoints[i];
        seg.end = extendedPoints[i + 1];
        seg.dir = (seg.end - seg.start).normalized();
        seg.up = seg.start.normalized();
        seg.right = QVector3D::crossProduct(seg.dir, seg.up).normalized();

        if (seg.right.length() < 0.001f) {
            seg.right = QVector3D(1.0f, 0.0f, 0.0f);
            seg.right = QVector3D::crossProduct(seg.up, seg.right).normalized();
        }
        seg.right.normalize();

        seg.startLeft = seg.start + seg.right * roadWidth;
        seg.startRight = seg.start - seg.right * roadWidth;
        seg.endLeft = seg.end + seg.right * roadWidth;
        seg.endRight = seg.end - seg.right * roadWidth;

        seg.startLeft = seg.startLeft.normalized() * (seg.startLeft.length() + heightOffset);
        seg.startRight = seg.startRight.normalized() * (seg.startRight.length() + heightOffset);
        seg.endLeft = seg.endLeft.normalized() * (seg.endLeft.length() + heightOffset);
        seg.endRight = seg.endRight.normalized() * (seg.endRight.length() + heightOffset);

        segments.push_back(seg);
    }

    // ===== 1. РИСУЕМ ВСЕ СЕГМЕНТЫ =====
    for (const auto& seg : segments) {
        vertices.insert(vertices.end(), {
            seg.startLeft.x(), seg.startLeft.y(), seg.startLeft.z(),
            seg.endLeft.x(), seg.endLeft.y(), seg.endLeft.z(),
            seg.startRight.x(), seg.startRight.y(), seg.startRight.z()
            });

        vertices.insert(vertices.end(), {
            seg.startRight.x(), seg.startRight.y(), seg.startRight.z(),
            seg.endLeft.x(), seg.endLeft.y(), seg.endLeft.z(),
            seg.endRight.x(), seg.endRight.y(), seg.endRight.z()
            });
    }

    // ===== 2. ЗАПОЛНЯЕМ ПОВОРОТЫ =====
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        const auto& prev = segments[i];
        const auto& curr = segments[i + 1];

        float dot = QVector3D::dotProduct(prev.dir, curr.dir);
        float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));

        if (angle > 0.05f) {
            QVector3D cross = QVector3D::crossProduct(prev.dir, curr.dir);
            float turnSign = QVector3D::dotProduct(cross, prev.up) > 0 ? 1.0f : -1.0f;

            QVector3D turnCenter;

            QVector3D p1 = prev.end - prev.right * roadWidth * turnSign;
            QVector3D p2 = curr.start - curr.right * roadWidth * turnSign;

            QVector3D d1 = prev.right * turnSign;
            QVector3D d2 = curr.right * turnSign;

            QVector3D diff = p2 - p1;

            QVector3D up = prev.up;
            QVector3D right = prev.right;
            QVector3D forward = prev.dir;

            float d1_r = QVector3D::dotProduct(d1, right);
            float d1_f = QVector3D::dotProduct(d1, forward);
            float d2_r = QVector3D::dotProduct(d2, right);
            float d2_f = QVector3D::dotProduct(d2, forward);
            float diff_r = QVector3D::dotProduct(diff, right);
            float diff_f = QVector3D::dotProduct(diff, forward);

            float det = d1_r * (-d2_f) - d1_f * (-d2_r);
            if (std::abs(det) > 0.0001f) {
                float t1 = (diff_r * (-d2_f) - diff_f * (-d2_r)) / det;
                turnCenter = p1 + d1 * t1;
            }
            else {
                turnCenter = (p1 + p2) * 0.5f;
            }

            turnCenter = turnCenter.normalized() * (turnCenter.length());

            int segmentsCount = std::max(6, int(cornerSegments * angle / 3.14159f));

            QVector3D prevLeft = prev.endLeft;
            QVector3D prevRight = prev.endRight;

            for (int s = 0; s <= segmentsCount; ++s) {
                float t = float(s) / float(segmentsCount);
                float rotAngle = t * angle * turnSign;

                QVector3D rotatedRight = QQuaternion::fromAxisAndAngle(
                    prev.up,
                    rotAngle * 180.0f / 3.14159f
                ).rotatedVector(prev.right * turnSign);

                QVector3D pointLeft = turnCenter + rotatedRight * roadWidth;
                QVector3D pointRight = turnCenter - rotatedRight * roadWidth;

                pointLeft = pointLeft.normalized() * (pointLeft.length() + heightOffset);
                pointRight = pointRight.normalized() * (pointRight.length() + heightOffset);

                if (s > 0) {
                    vertices.insert(vertices.end(), {
                        prevLeft.x(), prevLeft.y(), prevLeft.z(),
                        pointLeft.x(), pointLeft.y(), pointLeft.z(),
                        turnCenter.x(), turnCenter.y(), turnCenter.z()
                        });

                    vertices.insert(vertices.end(), {
                        prevRight.x(), prevRight.y(), prevRight.z(),
                        turnCenter.x(), turnCenter.y(), turnCenter.z(),
                        pointRight.x(), pointRight.y(), pointRight.z()
                        });

                    vertices.insert(vertices.end(), {
                        prevLeft.x(), prevLeft.y(), prevLeft.z(),
                        prevRight.x(), prevRight.y(), prevRight.z(),
                        pointLeft.x(), pointLeft.y(), pointLeft.z()
                        });

                    vertices.insert(vertices.end(), {
                        prevRight.x(), prevRight.y(), prevRight.z(),
                        pointRight.x(), pointRight.y(), pointRight.z(),
                        pointLeft.x(), pointLeft.y(), pointLeft.z()
                        });
                }

                prevLeft = pointLeft;
                prevRight = pointRight;
            }
        }
    }

    // ===== 3. ДОБАВЛЯЕМ ПОЛУКРУГИ НА КОНЦАХ =====
    if (segments.size() >= 1) {
        // ===== ПОЛУКРУГ В НАЧАЛЕ (смотрит назад) =====
        const auto& firstSeg = segments.front();
        QVector3D startCenter = firstSeg.start;
        QVector3D startUp = firstSeg.up;
        QVector3D startRight = firstSeg.right;

        // ===== ПОВОРАЧИВАЕМ НАЧАЛЬНУЮ ТОЧКУ НА 35 ГРАДУСОВ =====
        const float startRotationDegrees = 0.0f;  // Поворот против часовой стрелк
        // Поворачиваем startRight на 35 градусов против часовой стрелки
        QVector3D rotatedStartRight = QQuaternion::fromAxisAndAngle(
            startUp,
            -startRotationDegrees
        ).rotatedVector(startRight);

        // Левая точка теперь будет с противоположной стороны
        QVector3D startLeft = startCenter - rotatedStartRight * roadWidth;
        QVector3D startRightRotated = startCenter + rotatedStartRight * roadWidth;

        // Поднимаем над поверхностью
        startLeft = startLeft.normalized() * (startLeft.length() + heightOffset);
        startRightRotated = startRightRotated.normalized() * (startRightRotated.length() + heightOffset);

        // Начинаем с левого края (повёрнутого)
        QVector3D prevCapPoint = startLeft;

        // Закрываем левый угол
        vertices.insert(vertices.end(), {
            startCenter.x(), startCenter.y(), startCenter.z(),
            startLeft.x(), startLeft.y(), startLeft.z(),
            startRightRotated.x(), startRightRotated.y(), startRightRotated.z()
            });

        // Полукруг от повёрнутого левого края к повёрнутому правому
        for (int s = 1; s <= capSegments; ++s) {
            float t = float(s) / float(capSegments);
            float angle = t * 3.14159f;

            // Вращаем от -startRight (влево) через "спину" к startRight (вправо)
            QVector3D rotatedRight = QQuaternion::fromAxisAndAngle(
                startUp,
                angle * 180.0f / 3.14159f
            ).rotatedVector(-rotatedStartRight);

            QVector3D capPoint = startCenter + rotatedRight * roadWidth;
            capPoint = capPoint.normalized() * (capPoint.length() + heightOffset);

            vertices.insert(vertices.end(), {
                startCenter.x(), startCenter.y(), startCenter.z(),
                prevCapPoint.x(), prevCapPoint.y(), prevCapPoint.z(),
                capPoint.x(), capPoint.y(), capPoint.z()
                });

            prevCapPoint = capPoint;
        }

        // Закрываем правый угол
        vertices.insert(vertices.end(), {
            startCenter.x(), startCenter.y(), startCenter.z(),
            prevCapPoint.x(), prevCapPoint.y(), prevCapPoint.z(),
            startRightRotated.x(), startRightRotated.y(), startRightRotated.z()
            });

        // Полукруг в конце
        const auto& lastSeg = segments.back();
        QVector3D endCenter = lastSeg.end;
        QVector3D endUp = lastSeg.up;
        QVector3D endRight = lastSeg.right;

        prevCapPoint = lastSeg.endLeft;
        for (int s = 1; s <= capSegments; ++s) {
            float t = float(s) / float(capSegments);
            float angle = t * 3.14159f;

            QVector3D rotatedRight = QQuaternion::fromAxisAndAngle(
                endUp,
                angle * 180.0f / 3.14159f
            ).rotatedVector(endRight);

            QVector3D capPoint = endCenter + rotatedRight * roadWidth;
            capPoint = capPoint.normalized() * (capPoint.length() + heightOffset);

            vertices.insert(vertices.end(), {
                endCenter.x(), endCenter.y(), endCenter.z(),
                prevCapPoint.x(), prevCapPoint.y(), prevCapPoint.z(),
                capPoint.x(), capPoint.y(), capPoint.z()
                });

            prevCapPoint = capPoint;
        }
    }

    return vertices;
}


void HexSphereSceneController::rebuildPickTris() {
    model_.rebuildPickTris();
    qDebug() << "Rebuilt pickTris_ for" << model_.pickTris().size() << "triangles, L =" << L_;
}

int HexSphereSceneController::findCellByPosition(const QVector3D& position) const {
    const auto& cells = model_.cells();
    const auto& dual = model_.dualVerts();

    if (cells.empty() || dual.empty()) return -1;

    QVector3D dir = position.normalized();
    int bestCell = -1;
    float bestDot = -2.0f;

    // Ищем ячейку, чей центроид ближе всего к направлению
    for (const auto& cell : cells) {
        float dot = QVector3D::dotProduct(cell.centroid, dir);
        if (dot > bestDot) {
            bestDot = dot;
            bestCell = cell.id;
        }
    }

    // Дополнительная проверка: если позиция далеко от центроида,
    // проверяем все треугольники pickTris_
    if (bestCell >= 0) {
        const Cell& best = cells[static_cast<size_t>(bestCell)];
        float distToCentroid = (best.centroid - dir).length();
        if (distToCentroid > 0.3f) {
            const auto& pickTris = model_.pickTris();
            for (const auto& tri : pickTris) {
                // Проверяем, находится ли точка внутри треугольника
                QVector3D v0 = tri.v0.normalized();
                QVector3D v1 = tri.v1.normalized();
                QVector3D v2 = tri.v2.normalized();

                // Используем barycentric координаты
                QVector3D v0v1 = v1 - v0;
                QVector3D v0v2 = v2 - v0;
                QVector3D v0p = dir - v0;

                float d00 = QVector3D::dotProduct(v0v1, v0v1);
                float d01 = QVector3D::dotProduct(v0v1, v0v2);
                float d11 = QVector3D::dotProduct(v0v2, v0v2);
                float d20 = QVector3D::dotProduct(v0p, v0v1);
                float d21 = QVector3D::dotProduct(v0p, v0v2);

                float denom = d00 * d11 - d01 * d01;
                if (denom < 1e-6f) continue;

                float u = (d11 * d20 - d01 * d21) / denom;
                float v = (d00 * d21 - d01 * d20) / denom;
                float w = 1.0f - u - v;

                if (u >= -0.01f && v >= -0.01f && w >= -0.01f) {
                    bestCell = tri.cellId;
                    break;
                }
            }
        }
    }

    return bestCell;
}
