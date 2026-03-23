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

HexSphereSceneController::HexSphereSceneController()
    : generator_(std::make_unique<ClimateBiomeTerrainGenerator>()) {
    genParams_ = TerrainParams{ /*seed=*/12345u, /*seaLevel=*/3, /*scale=*/3.0f };
    rebuildModel();
}

void HexSphereSceneController::setGenerator(std::unique_ptr<ITerrainGenerator> generator) {
    generator_ = std::move(generator);
}

void HexSphereSceneController::setGeneratorByIndex(int idx) {
    switch (idx) {
    case 0: setGenerator(std::make_unique<NoOpTerrainGenerator>()); break;
    case 1: setGenerator(std::make_unique<SineTerrainGenerator>()); break;
    case 2: setGenerator(std::make_unique<PerlinTerrainGenerator>()); break;
    case 3: setGenerator(std::make_unique<ClimateBiomeTerrainGenerator>()); break;
    default: setGenerator(std::make_unique<ClimateBiomeTerrainGenerator>()); break;
    }
}

void HexSphereSceneController::setGenParams(const TerrainParams& params) {
    genParams_ = params;
}

void HexSphereSceneController::setSubdivisionLevel(int level) {
    if (L_ == level) {
        return;
    }
    L_ = level;
    heightStep_ = autoHeightStep();
    rebuildModel();
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

void HexSphereSceneController::rebuildModel() {
    ico_ = icoBuilder_.build(L_);
    model_.rebuildFromIcosphere(ico_);
    regenerateTerrain();
    generateTreePlacements();
}

void HexSphereSceneController::regenerateTerrain() {
    if (generator_) {
        generator_->generate(model_, genParams_);
    }
    updateTerrainMesh();
    generateTreePlacements();
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

    auto it = selectedCells_.begin();
    const int a = *it;
    ++it;
    const int b = *it;

    PathBuilder pb(model_);
    pb.build();
    auto ids = pb.astar(a, b);
    auto poly = pb.polylineOnSphere(ids, /*segmentsPerEdge=*/8, pathBias_, heightStep_);
    return poly;
}

std::vector<float> HexSphereSceneController::buildWireVertices() const {
    // WireMeshGenerator ожидае�? const HexSphereModel&
    return WireMeshGenerator::buildWireVertices(model_);
}

std::vector<float> HexSphereSceneController::buildSelectionOutlineVertices() const {
    // SelectionOutlineGenerator ожидае�?: const HexSphereModel&, const QSet<int>&, float, float, bool
    return SelectionOutlineGenerator::buildSelectionOutlineVertices(
        model_, selectedCells_, heightStep_, outlineBias_, smoothOneStep_);
}

WaterGeometryData HexSphereSceneController::buildWaterGeometry() const {
    return WaterMeshGenerator::buildWaterGeometry(model_);
}

float HexSphereSceneController::autoHeightStep() const {
    const float baseStep = 0.05f;
    const float reductionFactor = 0.4f;
    return baseStep / (1.0f + L_ * reductionFactor);
}

void HexSphereSceneController::updateTerrainMesh() {
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
    const auto& cells = model_.cells();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distBary(0.1f, 0.8f);
    std::uniform_real_distribution<float> distScale(0.8f, 1.2f);
    std::uniform_real_distribution<float> distRot(0.0f, 2.0f * 3.14159f);

    // �?Р�?�?�?НН�?: с�?авим де�?ев�?я на все кле�?ки с биомом Grass
    for (size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].biome == Biome::Grass) {  // Уб�?али �?словие i % 3 == 0
            TreePlacement placement;
            placement.cellId = static_cast<int>(i);

            if (!cells[i].poly.empty()) {
                std::uniform_int_distribution<int> distTri(0, cells[i].poly.size() - 1);
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

            placement.scale = distScale(gen);
            placement.rotation = distRot(gen);

            treePlacements_.push_back(placement);
        }
    }

    qDebug() << "Generated" << treePlacements_.size() << "tree placements (simple mode)";
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