#include "controllers/HexSphereSceneController.h"

#include <QtGlobal>
#include <algorithm>
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"
#include "generation/MeshGenerators/TerrainMeshGenerator.h"
#include "generation/MeshGenerators/WaterMeshGenerator.h"
#include "generation/MeshGenerators/WireMeshGenerator.h"
#include <QVector3D>
#include <qelapsedtimer.h>
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
}

void HexSphereSceneController::regenerateTerrain() {
    if (generator_) {
        generator_->generate(model_, genParams_);
    }
    updateTerrainMesh();
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
    return WireMeshGenerator::buildWireVertices(model_);
}

std::vector<float> HexSphereSceneController::buildSelectionOutlineVertices() const {
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

// ========== НОВЫЕ МЕТОДЫ ==========

// Преобразование плоского массива float в массив QVector3D
static std::vector<QVector3D> convertToQVector3D(const std::vector<float>& positions) {
    std::vector<QVector3D> result;
    result.reserve(positions.size() / 3);
    for (size_t i = 0; i < positions.size(); i += 3) {
        result.emplace_back(positions[i], positions[i + 1], positions[i + 2]);
    }
    return result;
}

// ОСНОВНАЯ ФУНКЦИЯ: фильтрация треугольников по видимости
//std::vector<uint32_t> HexSphereSceneController::getVisibleIndices(const QVector3D& cameraPos) const {
//    if (terrainCPU_.idx.empty() || terrainCPU_.pos.empty()) {
//        return terrainCPU_.idx;
//    }
//
//    // Валидируем/перестраиваем кэш при необходимости
//    validateCache();
//
//    QVector3D planetCenter(0, 0, 0);
//    QVector3D toCam = (cameraPos - planetCenter).normalized();
//
//    std::vector<uint32_t> visibleIndices;
//    visibleIndices.reserve(terrainCPU_.idx.size() / 2);
//
//    // Используем кэшированные центры
//    for (const auto& tri : triangleCache_) {
//        QVector3D normal = tri.center.normalized();
//
//        if (QVector3D::dotProduct(normal, toCam) > 0.0f) {
//            visibleIndices.push_back(tri.i0);
//            visibleIndices.push_back(tri.i1);
//            visibleIndices.push_back(tri.i2);
//        }
//    }
//
//    return visibleIndices;
//}

std::vector<uint32_t> HexSphereSceneController::getVisibleIndices(const QVector3D& cameraPos) const {
#ifdef __AVX2__
    return getVisibleIndicesSIMD(cameraPos);  // Используем SIMD если доступно
#else
    // Fallback на обычную версию
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
#endif
}

// Получить отфильтрованный TerrainMesh для текущей позиции камеры
TerrainMesh HexSphereSceneController::getVisibleTerrainMesh() const {
    TerrainMesh visibleMesh = terrainCPU_;  // Копируем весь mesh

    // Фильтруем индексы на основе текущей позиции камеры
    visibleMesh.idx = getVisibleIndices(cameraPos_);

    return visibleMesh;
}

// Обновить видимость (вызывается каждый кадр перед рендерингом)
void HexSphereSceneController::updateVisibility(const QVector3D& cameraPos) {
    // Сохраняем новую позицию камеры
    setCameraPosition(cameraPos);

    // Здесь можно добавить логику, если нужно что-то делать при изменении видимости
    // Например, отметить, что индексы нужно перезагрузить в GPU
}

// Получить статистику по видимости
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

    // Конвертируем позиции в QVector3D
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

    qDebug() << "Cache rebuilt:" << triangleCache_.size() << "triangles in"
        << timer.elapsed() << "ms";

    cacheValid_ = true;
}

void HexSphereSceneController::validateCache() const {
    if (!cacheValid_ || triangleCache_.size() != terrainCPU_.idx.size() / 3) {
        rebuildCache();
    }
}