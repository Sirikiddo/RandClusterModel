#include "TerrainCulling.h"
#include <QtDebug>

#include "renderers/TerrainTessellator.h" 

void TerrainCulling::setFullMesh(const TerrainMesh& mesh) {
    fullMesh_ = mesh;
    totalTriangles_ = mesh.idx.size() / 3;

    // Инициализируем culledMesh с теми же позициями/цветами/нормалями
    culledMesh_.pos = mesh.pos;
    culledMesh_.col = mesh.col;
    culledMesh_.norm = mesh.norm;

    buildTriangleCenters();

    // Изначально показываем все треугольники
    culledMesh_.idx = mesh.idx;
    culledMesh_.triOwner = mesh.triOwner;
    visibleTriangles_ = totalTriangles_;
}

void TerrainCulling::buildTriangleCenters() {
    triangleCenters_.clear();
    triangleCellOwners_.clear();

    const size_t triangleCount = fullMesh_.idx.size() / 3;
    triangleCenters_.reserve(triangleCount);
    triangleCellOwners_.reserve(triangleCount);

    VertexRef verts{ fullMesh_.pos };

    for (size_t i = 0; i + 2 < fullMesh_.idx.size(); i += 3) {
        uint32_t i0 = fullMesh_.idx[i];
        uint32_t i1 = fullMesh_.idx[i + 1];
        uint32_t i2 = fullMesh_.idx[i + 2];

        QVector3D v0 = verts(i0);
        QVector3D v1 = verts(i1);
        QVector3D v2 = verts(i2);

        QVector3D center = (v0 + v1 + v2) * (1.0f / 3.0f);
        triangleCenters_.push_back(center);

        if (i / 3 < fullMesh_.triOwner.size()) {
            triangleCellOwners_.push_back(fullMesh_.triOwner[i / 3]);
        }
    }

    qDebug() << "Built triangle cache:" << triangleCenters_.size() << "triangles";
}

void TerrainCulling::filterIndices(const QVector3D& cameraPos,
    const QVector3D& planetCenter,
    float eps) {
    if (triangleCenters_.empty()) return;

    QVector3D toCam = (cameraPos - planetCenter).normalized();

    std::vector<uint32_t> newIndices;
    std::vector<int> newOwners;

    newIndices.reserve(fullMesh_.idx.size() / 2); // Ожидаем ~50%
    if (!fullMesh_.triOwner.empty()) {
        newOwners.reserve(triangleCenters_.size() / 2);
    }

    VertexRef verts{ fullMesh_.pos };
    visibleTriangles_ = 0;

    for (size_t i = 0; i < triangleCenters_.size(); ++i) {
        const QVector3D& center = triangleCenters_[i];
        QVector3D n = (center - planetCenter).normalized();

        if (QVector3D::dotProduct(n, toCam) > eps) {
            // Треугольник видим - добавляем его индексы
            size_t idxPos = i * 3;
            newIndices.push_back(fullMesh_.idx[idxPos]);
            newIndices.push_back(fullMesh_.idx[idxPos + 1]);
            newIndices.push_back(fullMesh_.idx[idxPos + 2]);

            if (!fullMesh_.triOwner.empty() && i < fullMesh_.triOwner.size()) {
                newOwners.push_back(fullMesh_.triOwner[i]);
            }

            visibleTriangles_++;
        }
    }

    culledMesh_.idx = std::move(newIndices);
    culledMesh_.triOwner = std::move(newOwners);

    qDebug() << "Culling:" << visibleTriangles_ << "/" << totalTriangles_
        << "triangles visible (" << (visibleTriangles_ * 100 / totalTriangles_) << "%)";
}

const TerrainMesh& TerrainCulling::getCulledMesh(const QVector3D& cameraPos,
    const QVector3D& planetCenter,
    float eps) {
    filterIndices(cameraPos, planetCenter, eps);
    return culledMesh_;
}

void TerrainCulling::clearCache() {
    triangleCenters_.clear();
    triangleCellOwners_.clear();
    fullMesh_ = TerrainMesh{};
    culledMesh_ = TerrainMesh{};
}