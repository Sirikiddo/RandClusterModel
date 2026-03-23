#pragma once

#include <QVector3D>
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "renderers/TerrainTessellator.h" 

struct CulledMesh {
    std::vector<float> pos;      // оригинальные позиции (не меняются)
    std::vector<float> col;      // оригинальные цвета (не меняются)
    std::vector<float> norm;     // оригинальные нормали (не меняются)
    std::vector<uint32_t> idx;   // отфильтрованные индексы
    std::vector<int> triOwner;   // отфильтрованные владельцы
};

class TerrainCulling {
public:
    // Загрузить полный меш и подготовить кэш центров треугольников
    void setFullMesh(const TerrainMesh& mesh);

    // Получить меш с отсечением для данной позиции камеры
    const TerrainMesh& getCulledMesh(const QVector3D& cameraPos,
        const QVector3D& planetCenter,
        float eps = 0.0f);

    // Очистить кэш
    void clearCache();

    // Статистика
    size_t getVisibleTriangleCount() const { return visibleTriangles_; }
    size_t getTotalTriangleCount() const { return totalTriangles_; }

private:
    void buildTriangleCenters();
    void filterIndices(const QVector3D& cameraPos, const QVector3D& planetCenter, float eps);

    TerrainMesh fullMesh_;              // Оригинальный меш
    TerrainMesh culledMesh_;             // Меш с отфильтрованными индексами

    std::vector<QVector3D> triangleCenters_; // Кэш центров треугольников
    std::vector<int> triangleCellOwners_;     // Владельцы треугольников

    size_t totalTriangles_ = 0;
    size_t visibleTriangles_ = 0;

    // Кэш для быстрого доступа к позициям вершин
    struct VertexRef {
        const std::vector<float>& pos;
        QVector3D operator()(uint32_t idx) const {
            return QVector3D(pos[idx * 3], pos[idx * 3 + 1], pos[idx * 3 + 2]);
        }
    };
};