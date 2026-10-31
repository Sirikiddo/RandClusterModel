#include "controllers/HexSphereSceneController.h"

#include <QtGlobal>
#include <algorithm>
#include <random>
#include <cmath>
#include <utility>

// �?обавляем н�?жн�?е include
#include "generation/OreGenerator.h"
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"
#include <QVector3D>
#include <QElapsedTimer>

namespace {
    constexpr float kContributorTreeScale = 0.5f;

    float smoothstep01(float value, float edge0, float edge1) {
        if (edge1 <= edge0) {
            return value >= edge1 ? 1.0f : 0.0f;
        }
        const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    // Направление между центрами двух соседних клеток (для точек русла внутри ребра).
    QVector3D slerpDirection(const QVector3D& from, const QVector3D& to, float t) {
        const QVector3D blended = from * (1.0f - t) + to * t;
        if (blended.lengthSquared() < 1e-12f) {
            return from;
        }
        return blended.normalized();
    }

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
            && lhs.wavesEnabled == rhs.wavesEnabled
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
    model_.setTopology(buildHexSphereTopology(L_));
    model_.setBaseRadius(HexSphereModel::kDefaultBaseRadius);
    model_.setHeightStep(autoHeightStep());
    model_.setWaterSurfaceLevel(HexSphereModel::kDefaultWaterSurfaceLevel);
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
    generateRivers();
    generateTreePlacements();
}

void HexSphereSceneController::rebuildDerivedGeometry() {
    if (isContributorMode()) {
        rebuildContributorScene();
        return;
    }

    updateTerrainMesh();
    generateRivers();
    generateTreePlacements();
}

void HexSphereSceneController::clearForShutdown() {
    selectedCells_.clear();
    selectionOutlineVertices_.clear();
    selectionOutlineDirty_ = true;
    treePlacements_.clear();
    treeOccupiedCells_.clear();
    rivers_.clear();
    riverOccupiedCells_.clear();
    ++riverRevision_;
    triangleCache_.clear();
    cacheValid_ = false;
    velocity_ = QVector3D();
    cameraPos_ = QVector3D();
    lastCameraPos_ = QVector3D();
    terrainCPU_ = TerrainMesh{};
    coastalBand_ = CoastalBandData{};
    model_ = HexSphereModel{};
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
    snapshot.topology = model_.topology();
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

    // Деревья — препятствие для дороги. Отдаём пути ровно те клетки, на которых
    // деревья стоят сейчас (набор поддерживается setTreePlacements/generateTreePlacements).
    snapshot.treeCells.reserve(static_cast<size_t>(treeOccupiedCells_.size()));
    for (int cellId : treeOccupiedCells_) {
        if (cellId >= 0 && cellId < static_cast<int>(model_.cells().size())) {
            snapshot.treeCells.push_back(cellId);
        }
    }
    std::sort(snapshot.treeCells.begin(), snapshot.treeCells.end());

    // Реки — второй вид препятствия для дороги (наравне с деревьями). Набор
    // поддерживается generateRivers() и едет в path backend вместе с террейном.
    snapshot.riverCells.reserve(static_cast<size_t>(riverOccupiedCells_.size()));
    for (int cellId : riverOccupiedCells_) {
        if (cellId >= 0 && cellId < static_cast<int>(model_.cells().size())) {
            snapshot.riverCells.push_back(cellId);
        }
    }
    std::sort(snapshot.riverCells.begin(), snapshot.riverCells.end());

    return snapshot;
}

void HexSphereSceneController::applyTerrainSnapshot(const TerrainSnapshot& snapshot) {
    if (isContributorMode()) {
        return;
    }

    const bool topologyChanged =
        L_ != snapshot.subdivisionLevel || model_.cells().empty();

    generatorIndex_ = normalizeTerrainGeneratorIndex(snapshot.generatorIndex);
    generator_ = createTerrainGeneratorByIndex(generatorIndex_);
    genParams_ = snapshot.params;
    L_ = snapshot.subdivisionLevel;
    topologyDirty_ = false;

    // Terrain snapshots produced for a new seed keep the same subdivision.
    // Preserve the already-built topology and replace only mutable cell state.
    // When the snapshot carries its own immutable topology, adopt it instead of
    // rebuilding the same level a second time.
    if (topologyChanged) {
        if (snapshot.topology && snapshot.topology->subdivisionLevel == snapshot.subdivisionLevel) {
            if (model_.topology() != snapshot.topology || model_.cells().empty()) {
                model_.setTopology(snapshot.topology);
            }
            model_.setBaseRadius(HexSphereModel::kDefaultBaseRadius);
            model_.setHeightStep(autoHeightStep());
            model_.setWaterSurfaceLevel(HexSphereModel::kDefaultWaterSurfaceLevel);
        }
        else {
            rebuildTopology();
        }
    }

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
    generateRivers();
    generateTreePlacements();
}

float HexSphereSceneController::autoHeightStep() const {
    // Reduce the radial height step as subdivision increases so the finer
    // terrain stays visually flatter and does not overpower the sphere.
    const float baseStep = 0.05f;
    const float reductionFactor = 0.4f;
    return baseStep / (1.0f + L_ * reductionFactor);
}

void HexSphereSceneController::updateTerrainMesh() {
    if (isContributorMode()) {
        terrainCPU_ = TerrainMesh{};
        coastalBand_ = CoastalBandData{};
        cacheValid_ = false;
        triangleCache_.clear();
        return;
    }

    heightStep_ = autoHeightStep();
    ++terrainRevision_;
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

float HexSphereSceneController::treeGlobalScale() const {
    // Деревья масштабируются вместе с клетками: тот же коэффициент, что и у
    // машин/построек/дороги, поэтому относительные размеры объектов на планете
    // сохраняются при любом SubdivisionLevel.
    return getModelScaleFactor();
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

    // ========== CONTRIBUTOR MODE ==========
    if (isContributorMode()) {
        TreePlacement placement;
        placement.cellId = 0;
        // treeType убран — вид теперь определяется в рендерере через TreeBuilder
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

    // ========== PLANET MODE ==========
    const auto& cells = model_.cells();

    const uint32_t deterministicSeed =
        genParams_.seed ^
        (static_cast<uint32_t>(generatorIndex_ + 1) * 0x9e3779b9u) ^
        (static_cast<uint32_t>(L_ + 1) * 0x85ebca6bu);
    std::mt19937 gen(deterministicSeed);

    std::uniform_real_distribution<float> distBary(0.1f, 0.8f);
    // Разброс сугубо декоративный: мировой размер дерева задаётся топологией
    // (treeGlobalScale), а не случайным коэффициентом, иначе при смене
    // SubdivisionLevel дерево могло бы вырасти в разы.
    std::uniform_real_distribution<float> distScale(0.9f, 1.1f);
    std::uniform_real_distribution<float> distRot(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> distTreePresence(0.0f, 1.0f);

    int greenCount = 0;   // Grass/Jungle
    int savannaCount = 0; // Savanna

    for (size_t i = 0; i < cells.size(); ++i) {
        const auto& cell = cells[i];

        // ========== ТОЛЬКО GRASS, JUNGLE, SAVANNA ==========
        const bool isVegetationBiome =
            (cell.biome == Biome::Grass ||
                cell.biome == Biome::Jungle ||
                cell.biome == Biome::Savanna);
        if (!isVegetationBiome) {
            continue;
        }

        // ========== ВЫСОТА ==========
        if (cell.height >= 2) {
            continue;
        }

        // ========== НЕ В РУСЛЕ РЕКИ ==========
        // Деревья не растут в воде: клетки, занятые рекой, пропускаем.
        if (isCellOccupiedByRiver(static_cast<int>(i))) {
            continue;
        }

        // ========== НЕ РЯДОМ С ВОДОЙ ==========
        bool hasWaterNeighbor = false;
        for (int neighborId : cell.neighbors) {
            if (neighborId >= 0 &&
                neighborId < static_cast<int>(cells.size())) {
                if (cells[static_cast<size_t>(neighborId)].biome == Biome::Sea) {
                    hasWaterNeighbor = true;
                    break;
                }
            }
        }
        if (hasWaterNeighbor) {
            continue;
        }

        // ========== ПЛОТНОСТЬ ПО БИОМУ ==========
        bool shouldPlaceTree = false;
        if (cell.biome == Biome::Grass) {
            shouldPlaceTree = distTreePresence(gen) < 0.35f;
        }
        else if (cell.biome == Biome::Savanna) {
            shouldPlaceTree = distTreePresence(gen) < 0.25f;
        }
        else if (cell.biome == Biome::Jungle) {
            shouldPlaceTree = distTreePresence(gen) < 0.45f;
        }

        if (!shouldPlaceTree) {
            continue;
        }

        // ========== СОЗДАНИЕ PLACEMENT ==========
        // Вид, цвет и вариант дерева определяются позже в HexSphereRenderer
        // через TreeBuilder::selectSpecies + выбор варианта из кэша.
        TreePlacement placement;
        placement.cellId = static_cast<int>(i);
        // placement.treeType — УБРАНО

        if (!cell.poly.empty()) {
            std::uniform_int_distribution<int> distTri(
                0, static_cast<int>(cell.poly.size()) - 1);
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

        placement.rotation = distRot(gen);

        // ========== МАСШТАБ (без цветов!) ==========
        float s = distScale(gen);
        if (cell.biome == Biome::Jungle && cell.humidity > 0.7f) {
            s *= 1.06f;
        }
        else if (cell.biome == Biome::Savanna && cell.humidity < 0.3f) {
            s *= 0.94f;
        }
        placement.scale = s;

        // placement.foliageColor / trunkColor / colorType / isYellowCellTree — УБРАНО.
        // Всё это теперь берётся из TreeVariant в рендерере.

        if (cell.biome == Biome::Savanna) {
            ++savannaCount;
        }
        else {
            ++greenCount;
        }

        treePlacements_.push_back(placement);
    }

    qDebug() << "Generated" << treePlacements_.size() << "tree placements";
    qDebug() << "  - Green trees (Grass/Jungle):" << greenCount;
    qDebug() << "  - Savanna trees:" << savannaCount;

    updateTreeOccupiedCells();
}

void HexSphereSceneController::regenerateTreePlacements() {
    generateTreePlacements();
}

void HexSphereSceneController::generateRivers() {
    rivers_.clear();
    riverOccupiedCells_.clear();

    if (!isContributorMode() && !model_.cells().empty()) {
        const int cellCount = static_cast<int>(model_.cells().size());

        RiverGenerationOptions options;
        // Соль отделяет реки от расстановки деревьев при том же seed уровня.
        options.seed = genParams_.seed
            ^ (static_cast<uint32_t>(generatorIndex_ + 1) * 0x9e3779b9u)
            ^ (static_cast<uint32_t>(L_ + 1) * 0x85ebca6bu)
            ^ 0x52e3f1a7u;
        options.maxRivers = std::clamp(cellCount / 40, 2, 24);
        // Исток теперь в горах, поэтому реке нужно больше клеток, чтобы дойти
        // до берега. Потолок растёт вместе с детализацией, но остаётся конечным.
        options.maxRiverCells = std::clamp(12 + 4 * L_, 16, 40);
        options.minLandCells = 2;
        options.minSourceDrop = 2;
        options.searchNodeLimit = 1500;

        rivers_ = RiverGenerator::generate(model_, options);

        for (const RiverPath& river : rivers_) {
            for (int cellId : river.cellIds) {
                riverOccupiedCells_.insert(cellId);
            }
        }
    }

    ++riverRevision_;
    qDebug() << "Generated" << rivers_.size() << "rivers";
}

std::vector<float> HexSphereSceneController::buildRiverMesh(std::vector<float>* centerlineOut) const {
    if (centerlineOut) {
        centerlineOut->clear();
    }
    std::vector<float> vertices;
    if (isContributorMode() || rivers_.empty()) {
        return vertices;
    }

    const auto& cells = model_.cells();
    if (cells.empty()) {
        return vertices;
    }

    const int cellCount = static_cast<int>(cells.size());
    const float modelScale = getModelScaleFactor();
    const float waterRadius = model_.waterSurfaceRadius();
    const float waterLevel = model_.waterSurfaceLevel();
    // Русло слегка приподнято над поверхностью: иначе оно прячется в
    // микрорельефе и на кромках клеток. Подъём меньше шага высоты, поэтому вода
    // читается как поверхность, а не как висящая труба. У устья подъём
    // минимальный, чтобы язык реки сливался с морем.
    const float landBias = 0.55f * heightStep_;
    const float waterBias = 0.0035f * modelScale;
    // Особенность реки против дороги: русло шире у устьев и уже в среднем течении.
    const float mouthHalfWidth = kRiverMouthHalfWidth * modelScale;
    const float midHalfWidth = kRiverMidHalfWidth * modelScale;

    // Русло должно ложиться на фактическую поверхность террейна, а не на
    // аналитическую высоту клеток: тесселятор сглаживает кромки и строит
    // клиффы, поэтому расчётная высота на неровных клетках не совпадает с
    // мешем. Сэмплируем реальные треугольники террейна по направлению и
    // приподнимаем воду ровно на surfaceBias.
    const bool hasTerrainMesh = !terrainCPU_.pos.empty()
        && !terrainCPU_.idx.empty()
        && !terrainCPU_.triOwner.empty();
    std::vector<std::vector<uint32_t>> cellTriangles;
    if (hasTerrainMesh) {
        const size_t triCount = std::min(
            terrainCPU_.idx.size() / 3, terrainCPU_.triOwner.size());
        cellTriangles.resize(static_cast<size_t>(cellCount));
        for (size_t tri = 0; tri < triCount; ++tri) {
            const int owner = terrainCPU_.triOwner[tri];
            if (owner >= 0 && owner < cellCount) {
                cellTriangles[static_cast<size_t>(owner)].push_back(static_cast<uint32_t>(tri));
            }
        }
    }

    auto terrainRadiusAt = [&](const QVector3D& direction, int hintCell) -> float {
        if (!hasTerrainMesh || hintCell < 0 || hintCell >= cellCount) {
            const int fallback = std::clamp(hintCell, 0, std::max(cellCount - 1, 0));
            return model_.radiusForHeight(
                cells.empty() ? 0.0f : static_cast<float>(cells[static_cast<size_t>(fallback)].height));
        }

        const float* positions = terrainCPU_.pos.data();
        const uint32_t* indices = terrainCPU_.idx.data();
        auto vertex = [&](uint32_t index) {
            return QVector3D(
                positions[index * 3 + 0],
                positions[index * 3 + 1],
                positions[index * 3 + 2]);
            };

        float best = -1.0f;
        auto testCell = [&](int cellId) {
            if (cellId < 0 || cellId >= cellCount) {
                return;
            }
            for (uint32_t tri : cellTriangles[static_cast<size_t>(cellId)]) {
                const QVector3D v0 = vertex(indices[tri * 3 + 0]);
                const QVector3D v1 = vertex(indices[tri * 3 + 1]);
                const QVector3D v2 = vertex(indices[tri * 3 + 2]);
                const QVector3D e1 = v1 - v0;
                const QVector3D e2 = v2 - v0;
                const QVector3D pvec = QVector3D::crossProduct(direction, e2);
                const float det = QVector3D::dotProduct(e1, pvec);
                if (std::abs(det) < 1e-9f) {
                    continue;
                }
                const float invDet = 1.0f / det;
                const QVector3D tvec = -v0;
                const float u = QVector3D::dotProduct(tvec, pvec) * invDet;
                if (u < 0.0f || u > 1.0f) {
                    continue;
                }
                const QVector3D qvec = QVector3D::crossProduct(tvec, e1);
                const float v = QVector3D::dotProduct(direction, qvec) * invDet;
                if (v < 0.0f || u + v > 1.0f) {
                    continue;
                }
                const float distance = QVector3D::dotProduct(e2, qvec) * invDet;
                if (distance > 1e-6f) {
                    best = std::max(best, distance);
                }
            }
            };

        testCell(hintCell);
        for (int neighbor : cells[static_cast<size_t>(hintCell)].neighbors) {
            testCell(neighbor);
        }

        if (best > 0.0f) {
            return best;
        }
        return model_.radiusForHeight(
            static_cast<float>(cells[static_cast<size_t>(hintCell)].height));
    };

    // Русло повторяет профиль рельефа. Тесселятор между клетками строит скос
    // (bladeHeightForEdge: среднее высот клеток при перепаде не больше
    // smoothMaxDelta, иначе — высота самой клетки), поэтому и русло идёт так же:
    // плато клетки → скос к общей кромке → скос соседа → плато соседа.
    // Иначе русло либо проваливается под рельеф, либо совпадает со скосом и
    // пропадает из-за z-fighting (в частности, у берега скос стоит ровно на
    // уровне моря).
    constexpr int kSubStepsPerEdge = 8;
    // Кромка внутреннего плато: тесселятор вдавливает плато на stripInset от границы,
    // поэтому скос занимает последние stripInset клетки (при inset 0.25 это 3/8..5/8).
    const float bladeStart = std::clamp(0.5f * (1.0f - stripInset_), 0.10f, 0.45f);
    const float bladeEnd = 1.0f - bladeStart;

    constexpr int kCrossCount = 5;
    constexpr float kCross[kCrossCount] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };

    struct Sample {
        QVector3D dir;
        float radius = 0.0f;
        int cellHint = -1;
    };
    struct Section {
        QVector3D center;
        QVector3D right;
        QVector3D up;
        float halfWidth = 0.0f;
        int cellHint = -1;
    };

    std::vector<Sample> samples;
    std::vector<Section> sections;

    const int smoothDelta = smoothOneStep_ ? 1 : 0;
    auto mixf = [](float a, float b, float t) { return a + (b - a) * t; };

    // Высота кромки со стороны клетки — как TerrainTessellator::bladeHeightForEdge.
    auto edgeHeight = [smoothDelta](const Cell& self, const Cell& other) {
        if (std::abs(self.height - other.height) <= smoothDelta) {
            return 0.5f * (static_cast<float>(self.height) + static_cast<float>(other.height));
        }
        return static_cast<float>(self.height);
    };

    // Радиус поверхности для высоты: у воды — не ниже поверхности моря.
    auto surfaceRadius = [&](float height) {
        float radius = model_.radiusForHeight(height);
        if (height <= waterLevel) {
            radius = waterRadius;
        }
        return radius + landBias;
    };

    for (const RiverPath& river : rivers_) {
        if (river.cellIds.size() < 3) {
            continue;
        }

        // Сначала проверяем весь путь: дальше мы свободно обращаемся к соседним клеткам.
        const bool allCellsValid = std::all_of(
            river.cellIds.begin(), river.cellIds.end(),
            [cellCount](int cellId) { return cellId >= 0 && cellId < cellCount; });
        if (!allCellsValid) {
            continue;
        }

        samples.clear();
        const size_t count = river.cellIds.size();
        // Исток реки — горная клетка, устье — морская. Внутрь моря русло не
        // заводим: поверхность воды там рисует сам WaterRenderer, и лента,
        // ушедшая под морскую клетку, читалась как провал под водой. Обрезаем
        // по кромке клетки (t = 0.5 ребра) — река впадает в море.
        const Cell& firstCell = cells[static_cast<size_t>(river.cellIds.front())];
        const Cell& lastCell = cells[static_cast<size_t>(river.cellIds.back())];
        const bool startsInSea = firstCell.biome == Biome::Sea;
        const bool endsInSea = lastCell.biome == Biome::Sea;

        for (size_t i = 0; i + 1 < count; ++i) {
            const Cell& from = cells[static_cast<size_t>(river.cellIds[i])];
            const Cell& to = cells[static_cast<size_t>(river.cellIds[i + 1])];
            const QVector3D dirFrom = from.centroid.normalized();
            const QVector3D dirTo = to.centroid.normalized();

            const float edgeFrom = edgeHeight(from, to);
            const float edgeTo = edgeHeight(to, from);

            for (int k = 0; k < kSubStepsPerEdge; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(kSubStepsPerEdge);
                if (startsInSea && i == 0 && t < 0.5f) {
                    continue;
                }
                if (endsInSea && i + 2 == count && t > 0.5f) {
                    continue;
                }

                float height = static_cast<float>(from.height);
                if (t > bladeStart && t <= 0.5f) {
                    height = mixf(static_cast<float>(from.height), edgeFrom,
                        (t - bladeStart) / (0.5f - bladeStart));
                }
                else if (t > 0.5f && t <= bladeEnd) {
                    height = mixf(edgeTo, static_cast<float>(to.height),
                        (t - 0.5f) / (bladeEnd - 0.5f));
                }
                else if (t > bladeEnd) {
                    height = static_cast<float>(to.height);
                }

                Sample sample;
                sample.dir = slerpDirection(dirFrom, dirTo, t);
                sample.radius = surfaceRadius(height);
                sample.cellHint = (t <= 0.5f) ? from.id : to.id;
                samples.push_back(sample);
            }
        }
        if (!endsInSea) {
            const Cell& last = cells[static_cast<size_t>(river.cellIds.back())];
            Sample sample;
            sample.dir = last.centroid.normalized();
            sample.radius = surfaceRadius(static_cast<float>(last.height));
            sample.cellHint = last.id;
            samples.push_back(sample);
        }

        if (samples.size() < 3) {
            continue;
        }

        // Высоту каждого сечения берём с фактического меша террейна.
        for (Sample& sample : samples) {
            sample.radius = terrainRadiusAt(sample.dir, sample.cellHint) + landBias;
        }

        // Устье: строим наклонный ramp от кромки берега до уровня моря внутри
        // морской клетки. Вертикальная стенка у берега сверху не видна, а ramp
        // читается как водопад, впадающий в море.
        const float mouthRadius = waterRadius + waterBias;
        if (endsInSea) {
            const QVector3D mouthDir =
                cells[static_cast<size_t>(river.cellIds.back())].centroid.normalized();
            const QVector3D landDir =
                cells[static_cast<size_t>(river.cellIds[count - 2])].centroid.normalized();
            const float shoreRadius = samples.back().radius;
            constexpr int kMouthRampSteps = 6;
            for (int step = 1; step <= kMouthRampSteps; ++step) {
                const float t = static_cast<float>(step) / static_cast<float>(kMouthRampSteps);
                Sample water;
                water.dir = slerpDirection(landDir, mouthDir, 0.5f + 0.45f * t);
                water.radius = mixf(shoreRadius, mouthRadius, t);
                water.cellHint = river.cellIds.back();
                samples.push_back(water);
            }
        }

        sections.clear();
        sections.reserve(samples.size());
        for (size_t i = 0; i < samples.size(); ++i) {
            const QVector3D up = samples[i].dir;

            // Касательная по центральной разности: на повороте сечение идёт по
            // биссектрисе, поэтому русло не «ломается» на углах между клетками.
            QVector3D tangent;
            if (i > 0 && i + 1 < samples.size()) {
                tangent = samples[i + 1].dir - samples[i - 1].dir;
            }
            else if (i + 1 < samples.size()) {
                tangent = samples[i + 1].dir - up;
            }
            else {
                tangent = up - samples[i - 1].dir;
            }
            tangent -= QVector3D::dotProduct(tangent, up) * up;
            if (tangent.lengthSquared() < 1e-10f) {
                tangent = QVector3D::crossProduct(up, QVector3D(0.0f, 1.0f, 0.0f));
            }
            tangent.normalize();

            QVector3D right = QVector3D::crossProduct(tangent, up);
            if (right.lengthSquared() < 1e-10f) {
                right = QVector3D(1.0f, 0.0f, 0.0f);
            }
            right.normalize();

            // На крутом повороте русло чуть сужается: иначе внешняя сторона
            // самопересекается и выглядит сложенной.
            float turnScale = 1.0f;
            if (i > 0 && i + 1 < samples.size()) {
                const QVector3D inDir = (up - samples[i - 1].dir).normalized();
                const QVector3D outDir = (samples[i + 1].dir - up).normalized();
                const float cosTurn = std::clamp(
                    QVector3D::dotProduct(inDir, outDir), -1.0f, 1.0f);
                turnScale = std::clamp(0.55f + 0.45f * cosTurn, 0.45f, 1.0f);
            }

            const float t = static_cast<float>(i) / static_cast<float>(samples.size() - 1);
            // Устье широкое, исток — узкий родник: река расширяется по течению,
            // а не начинается сразу широкой лентой на горном склоне.
            const float mouthBlend = smoothstep01(1.0f - t, 0.0f, 0.3f);
            const float sourceTaper = mixf(0.50f, 1.0f, smoothstep01(t, 0.0f, 0.16f));
            const float wobble = 0.85f
                + 0.30f * std::sin(t * 12.3f + static_cast<float>(river.cellIds.front()) * 0.7f);

            Section section;
            section.center = up * samples[i].radius;
            section.right = right;
            section.up = up;
            // Минимальная ширина: на крутых склонах и поворотах лента иначе
            // сжимается до пары пикселей и выглядит разрывом.
            const float profileHalfWidth =
                (midHalfWidth + (mouthHalfWidth - midHalfWidth) * mouthBlend)
                * wobble * turnScale * sourceTaper;
            section.halfWidth = std::max(profileHalfWidth, 0.60f * midHalfWidth);
            section.cellHint = samples[i].cellHint;
            sections.push_back(section);
        }

        if (centerlineOut && sections.size() >= 2) {
            centerlineOut->reserve(centerlineOut->size() + (sections.size() - 1) * 6);
            for (size_t i = 0; i + 1 < sections.size(); ++i) {
                const QVector3D& a = sections[i].center;
                const QVector3D& b = sections[i + 1].center;
                centerlineOut->insert(centerlineOut->end(), {
                    a.x(), a.y(), a.z(), b.x(), b.y(), b.z() });
            }
        }

        // 11 float на вершину: позиция, (берег, глубина), нормаль, руда.
        auto emitSectionVertex = [&](const Section& section, float offset) {
            const float bank = std::abs(offset);
            const float depth = 1.0f - bank;
            const QVector3D centerDir = section.center.normalized();
            const QVector3D lateralDir = (centerDir
                + section.right * (offset * section.halfWidth)).normalized();
            // Вода держится на уровне сечения, но у более высокого берега
            // поднимается до кромки террейна, чтобы не уходить под рельеф.
            const float bankRadius =
                terrainRadiusAt(lateralDir, section.cellHint) + landBias;
            const float radius = std::max(section.center.length(), bankRadius);
            const QVector3D point = lateralDir * radius;
            const QVector3D& normal = section.up;
            vertices.insert(vertices.end(), {
                point.x(), point.y(), point.z(),
                bank, depth, 0.0f,
                normal.x(), normal.y(), normal.z(),
                0.0f, 0.0f
                });
            };

        for (size_t i = 0; i + 1 < sections.size(); ++i) {
            const Section& from = sections[i];
            const Section& to = sections[i + 1];
            for (int k = 0; k + 1 < kCrossCount; ++k) {
                emitSectionVertex(from, kCross[k]);
                emitSectionVertex(from, kCross[k + 1]);
                emitSectionVertex(to, kCross[k + 1]);

                emitSectionVertex(from, kCross[k]);
                emitSectionVertex(to, kCross[k + 1]);
                emitSectionVertex(to, kCross[k]);
            }
        }
    }

    return vertices;
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
    generateRivers();
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

    // A triangle whose center is just behind the analytic horizon can still
    // contribute visible pixels because its vertices span the silhouette.
    // The old zero threshold cut those triangles wholesale; at L6/L7 that
    // produced a grainy horizon and large holes. Keep a conservative band and
    // let depth/back-face testing remove the truly hidden fragments.
    constexpr float kHorizonGuardBand = -0.05f;

    for (const auto& tri : triangleCache_) {
        QVector3D normal = tri.center.normalized();
        if (QVector3D::dotProduct(normal, toCam) > kHorizonGuardBand) {
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
