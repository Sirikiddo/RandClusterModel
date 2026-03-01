#include "WaterMeshGenerator.h"

#include <functional>

WaterGeometryData WaterMeshGenerator::buildWaterGeometry(const HexSphereModel& model) {
    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();

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
