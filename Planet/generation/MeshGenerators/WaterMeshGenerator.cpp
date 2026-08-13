#include "WaterMeshGenerator.h"

#include <cstddef>
#include <functional>

namespace {

QVector3D positionAt(const QVector3D& unitDir, float baseRadius) {
    QVector3D dir = unitDir;
    if (!dir.isNull()) {
        dir.normalize();
    }
    return dir * baseRadius;
}

} // namespace

WaterGeometryData WaterMeshGenerator::buildWaterGeometry(
    const HexSphereModel& model) {
    WaterGeometryData data;

    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();
    constexpr float proxyRadius = 1.0f;
    const unsigned int waterSubdivisions = 2;

    std::function<void(const QVector3D&, const QVector3D&, const QVector3D&, unsigned int)> subdivideTriangle;
    subdivideTriangle =
        [&](const QVector3D& v0,
            const QVector3D& v1,
            const QVector3D& v2,
            unsigned int level) {
            if (level == 0) {
                const unsigned int i0 = static_cast<unsigned int>(data.positions.size() / 3);
                data.positions.insert(data.positions.end(), { v0.x(), v0.y(), v0.z() });

                const unsigned int i1 = static_cast<unsigned int>(data.positions.size() / 3);
                data.positions.insert(data.positions.end(), { v1.x(), v1.y(), v1.z() });

                const unsigned int i2 = static_cast<unsigned int>(data.positions.size() / 3);
                data.positions.insert(data.positions.end(), { v2.x(), v2.y(), v2.z() });

                data.indices.insert(data.indices.end(), { i0, i1, i2 });
                return;
            }

            const QVector3D d0 = v0.normalized();
            const QVector3D d1 = v1.normalized();
            const QVector3D d2 = v2.normalized();

            const QVector3D mid01 = positionAt((d0 + d1) * 0.5f, proxyRadius);
            const QVector3D mid12 = positionAt((d1 + d2) * 0.5f, proxyRadius);
            const QVector3D mid20 = positionAt((d2 + d0) * 0.5f, proxyRadius);

            subdivideTriangle(v0, mid01, mid20, level - 1);
            subdivideTriangle(mid01, v1, mid12, level - 1);
            subdivideTriangle(mid20, mid12, v2, level - 1);
            subdivideTriangle(mid01, mid12, mid20, level - 1);
        };

    for (size_t cellIdx = 0; cellIdx < cells.size(); ++cellIdx) {
        const auto& cell = cells[cellIdx];
        // The mesh is conservative coverage only. Semantic Sea/Land rejection
        // happens after the analytic hit, from the hydrology atlas.
        if (cell.poly.size() < 3) {
            continue;
        }

        const QVector3D center = positionAt(cell.centroid, proxyRadius);
        std::vector<QVector3D> vertices;
        vertices.reserve(cell.poly.size());

        for (int dv : cell.poly) {
            const QVector3D& unit = dual[static_cast<size_t>(dv)];
            vertices.push_back(positionAt(unit, proxyRadius));
        }

        for (size_t i = 0; i < vertices.size(); ++i) {
            const size_t next = (i + 1) % vertices.size();
            subdivideTriangle(center, vertices[i], vertices[next], waterSubdivisions);
        }
    }

    return data;
}
