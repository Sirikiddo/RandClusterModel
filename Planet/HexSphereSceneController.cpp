#include "HexSphereSceneController.h"

#include <QtGlobal>
#include <algorithm>
#include <functional>

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
    const auto& vertices = model_.dualVerts();
    std::vector<float> lineVerts;
    lineVerts.reserve(model_.wireEdges().size() * 6);

    for (auto [a, b] : model_.wireEdges()) {
        const auto& pa = vertices[size_t(a)];
        const auto& pb = vertices[size_t(b)];
        lineVerts.insert(lineVerts.end(), { pa.x(), pa.y(), pa.z(), pb.x(), pb.y(), pb.z() });
    }

    return lineVerts;
}

std::vector<float> HexSphereSceneController::buildSelectionOutlineVertices() const {
    const auto& cells = model_.cells();
    const auto& dual = model_.dualVerts();
    constexpr float R = 1.0f;

    std::vector<float> lineVerts;
    lineVerts.reserve(selectedCells_.size() * 12);

    auto liftBias = [&](const QVector3D& u, float h) { return u.normalized() * (R + h * heightStep_ + outlineBias_); };

    for (int cid : selectedCells_) {
        const auto& c = cells[size_t(cid)];
        const int deg = int(c.poly.size());
        for (int i = 0; i < deg; ++i) {
            const int j = (i + 1) % deg;
            const int va = c.poly[i];
            const int vb = c.poly[j];
            float hA = float(c.height), hB = float(c.height);
            const int nEdge = c.neighbors[i];
            if (nEdge >= 0) {
                const int hN = cells[size_t(nEdge)].height;
                const int d = std::abs(hN - c.height);
                if (smoothOneStep_ && d == 1) {
                    const float mid = 0.5f * float(hN + c.height);
                    hA = hB = mid;
                }
            }
            const QVector3D pA = liftBias(dual[size_t(va)], hA);
            const QVector3D pB = liftBias(dual[size_t(vb)], hB);
            lineVerts.insert(lineVerts.end(), { pA.x(), pA.y(), pA.z(), pB.x(), pB.y(), pB.z() });
        }
    }

    return lineVerts;
}

WaterGeometryData HexSphereSceneController::buildWaterGeometry() const {
    const auto& cells = model_.cells();
    const auto& dual = model_.dualVerts();

    WaterGeometryData data;

    const float SEA_LEVEL = 1.0f;
    const unsigned int WATER_SUBDIVISIONS = 3;

    std::function<void(const QVector3D&, const QVector3D&, const QVector3D&, float, float, float, unsigned int)> subdivideTriangle;
    subdivideTriangle = [&](const QVector3D& v0, const QVector3D& v1, const QVector3D& v2, float edge0, float edge1, float edge2, unsigned int level) {
        if (level <= 0) {
            unsigned int i0 = static_cast<unsigned int>(data.positions.size() / 3);
            data.positions.insert(data.positions.end(), { v0.x(), v0.y(), v0.z() });
            data.edgeFlags.push_back(edge0);

            unsigned int i1 = static_cast<unsigned int>(data.positions.size() / 3);
            data.positions.insert(data.positions.end(), { v1.x(), v1.y(), v1.z() });
            data.edgeFlags.push_back(edge1);

            unsigned int i2 = static_cast<unsigned int>(data.positions.size() / 3);
            data.positions.insert(data.positions.end(), { v2.x(), v2.y(), v2.z() });
            data.edgeFlags.push_back(edge2);

            data.indices.insert(data.indices.end(), { i0, i1, i2 });
            return;
        }

        QVector3D mid01 = (v0 + v1) * 0.5f;
        QVector3D mid12 = (v1 + v2) * 0.5f;
        QVector3D mid20 = (v2 + v0) * 0.5f;

        mid01 = mid01.normalized() * SEA_LEVEL;
        mid12 = mid12.normalized() * SEA_LEVEL;
        mid20 = mid20.normalized() * SEA_LEVEL;

        float edge_mid01 = (edge0 + edge1) * 0.5f;
        float edge_mid12 = (edge1 + edge2) * 0.5f;
        float edge_mid20 = (edge2 + edge0) * 0.5f;

        subdivideTriangle(v0, mid01, mid20, edge0, edge_mid01, edge_mid20, level - 1);
        subdivideTriangle(mid01, v1, mid12, edge_mid01, edge1, edge_mid12, level - 1);
        subdivideTriangle(mid20, mid12, v2, edge_mid20, edge_mid12, edge2, level - 1);
        subdivideTriangle(mid01, mid12, mid20, edge_mid01, edge_mid12, edge_mid20, level - 1);
    };

    for (size_t cellIdx = 0; cellIdx < cells.size(); ++cellIdx) {
        const auto& cell = cells[cellIdx];
        if (cell.biome != Biome::Sea || cell.poly.size() < 3) {
            continue;
        }

        QVector3D center = cell.centroid.normalized() * SEA_LEVEL;

        std::vector<QVector3D> vertices;
        std::vector<float> vertexEdgeFlags;

        for (int dv : cell.poly) {
            const QVector3D& vert = dual[static_cast<size_t>(dv)];
            QVector3D waterVert = vert.normalized() * SEA_LEVEL;
            vertices.push_back(waterVert);
            vertexEdgeFlags.push_back(1.0f);
        }

        const size_t numVertices = vertices.size();
        for (size_t i = 0; i < numVertices; ++i) {
            size_t next_i = (i + 1) % numVertices;
            subdivideTriangle(center, vertices[i], vertices[next_i], 0.0f, vertexEdgeFlags[i], vertexEdgeFlags[next_i], WATER_SUBDIVISIONS);
        }
    }

    return data;
}

float HexSphereSceneController::autoHeightStep() const {
    const float baseStep = 0.05f;
    const float reductionFactor = 0.4f;
    return baseStep / (1.0f + L_ * reductionFactor);
}

void HexSphereSceneController::updateTerrainMesh() {
    TerrainTessellator tt;
    tt.R = 1.0f;
    heightStep_ = autoHeightStep();
    tt.heightStep = heightStep_;
    tt.inset = stripInset_;
    tt.smoothMaxDelta = smoothOneStep_ ? 1 : 0;
    tt.outerTrim = 0.15f;
    tt.doCaps = true;
    tt.doBlades = true;
    tt.doCornerTris = true;
    tt.doEdgeCliffs = true;

    terrainCPU_ = tt.build(model_);
}
