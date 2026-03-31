#pragma once
#include <QVector3D>
#include <QMatrix4x4>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <limits>
#include <memory>

struct Tri { int a, b, c; };

// Simple 64-bit key for undirected edge (i<j)
struct EdgeKey {
    uint64_t k;
    EdgeKey() : k(0) {}
    EdgeKey(uint32_t i, uint32_t j) {
        if (i > j) std::swap(i, j);
        k = (uint64_t(i) << 32) | uint64_t(j);
    }
    bool operator==(const EdgeKey& o) const { return k == o.k; }
};

struct EdgeKeyHash { size_t operator()(const EdgeKey& e) const noexcept { return std::hash<uint64_t>{}(e.k); } };

// Icosphere primal mesh
struct IcoMesh {
    std::vector<QVector3D> P; // unit-sphere vertices
    std::vector<Tri> F;       // triangles (ccw)
    std::vector<std::array<int, 3>> Fv; // each face's 3 vertex indices (alias to Tri)
    std::vector<std::vector<int>> incidentFaces; // per vertex -> list of face indices
};

class IcosphereBuilder {
public:
    IcoMesh build(int level) const; // level >= 0
private:
    static std::pair<std::vector<QVector3D>, std::vector<Tri>> baseIcosahedron();
};

enum class Biome : uint8_t {
    Sea = 0,
    Grass = 1,
    Rock = 2,
    Snow = 3,
    Tundra = 4,
    Desert = 5,
    Savanna = 6,
    Jungle = 7
};

enum class TreeType : uint8_t {
    Oak = 0,      // Обычное дерево (дуб)
    Fir = 1       // Ёлочка
};

struct OreVisualParams {
    float density = 0.0f;           // Плотность [0-1]
    float grainSize = 0.05f;        // Размер зерна (0.01-0.1)
    float grainContrast = 1.0f;     // Контрастность зерен (1-3)
    QVector3D baseColor;            // Базовый цвет руды
    QVector3D grainColor;           // Цвет зерен
};

// Forward declaration
class HexSphereModel;

// Structure for tree placement with random position within cell
struct TreePlacement {
    int cellId = -1;
    int triangleIdx = 0;
    float baryU = 0.33f;
    float baryV = 0.33f;
    float baryW = 0.34f;
    float scale = 1.0f;
    float rotation = 0.0f;

    // НОВОЕ: тип дерева
    TreeType treeType = TreeType::Oak;

    // Цвета листвы и ствола
    enum class TreeColorType : uint8_t {
        Green = 0,
        Autumn = 1
    };

    TreeColorType colorType = TreeColorType::Green;
    QVector3D foliageColor = QVector3D(0.2f, 0.55f, 0.15f);
    QVector3D trunkColor = QVector3D(0.5f, 0.35f, 0.2f);
    bool isYellowCellTree = false;

    QVector3D getPosition(const HexSphereModel& model) const;
};

// Dual (hex/pent) sphere data
struct Cell {
    int id = -1;                 // equals primal vertex index
    bool isPentagon = false;     // degree==5
    std::vector<int> poly;       // indices into dualVerts (centers of triangles), CCW around cell
    std::vector<int> neighbors;  // neighbor cell ids CCW (same length as poly)
    int height = 0;                 // дискретная высота
    Biome biome = Biome::Grass;     // тип биома
    QVector3D centroid;          // normalized average of poly vertices
    float area = 0.0f;           // euclidean triangle-fan area (for info)
    uint32_t stateMask = 0;      // bit 0 => selected

    // Климатические данные (из старой версии)
    float temperature = 0.0f;    // температура [0..1]
    float humidity = 0.0f;       // влажность [0..1] 
    float pressure = 0.0f;       // давление [0..1]

    // Данные о руде (из старой версии)
    float oreDensity = 0.0f;     // плотность руды [0..1]
    uint8_t oreType = 0;         // тип руды (0-нет, 1-железо, 2-медь, и т.д.)
    OreVisualParams oreVisual;   // Визуальные параметры руды
    float oreNoiseOffset = 0.0f; // Смещение для анимации шума
};

struct PickTri { // geometry for ray picking
    int cellId;
    QVector3D v0, v1, v2; // triangle positions (world)
};

class HexSphereModel {
public:
    void rebuildFromIcosphere(const IcoMesh& ico);

    const std::vector<QVector3D>& dualVerts() const { return dualVerts_; }
    const std::vector<std::pair<int, int>>& wireEdges() const { return wireEdges_; }
    const std::vector<Cell>& cells() const { return cells_; }
    std::vector<Cell>& cells() { return cells_; }
    const std::vector<PickTri>& pickTris() const { return pickTris_; }
    const std::vector<std::array<int, 3>>& dualOwners() const { return dualOwners_; }

    int subdivisions() const { return L_; }
    int pentagonCount() const { return pentCount_; }
    int cellCount() const { return static_cast<int>(cells_.size()); }

    // Удобные сеттеры
    void setHeight(int cellId, int h);
    void addHeight(int cellId, int dh);
    void setBiome(int cellId, Biome b);

    // Сеттеры для климатических данных
    void setTemperature(int cellId, float temp);
    void setHumidity(int cellId, float humidity);
    void setPressure(int cellId, float pressure);
    void setOreDensity(int cellId, float oreDensity);
    void setOreType(int cellId, uint8_t oreType);

    // Утилиты для климатических данных
    float getAverageTemperature() const;
    float getAverageHumidity() const;
    std::vector<int> getCellsWithOre(uint8_t oreType) const;
    void resetClimateData();

    // Утилиты
    static QVector3D biomeColor(Biome b, float temperature = 0.5f);

    // Для тестирования
    void debug_setCellsAndDual(std::vector<Cell> c, std::vector<QVector3D> d) {
        cells_ = std::move(c);
        dualVerts_ = std::move(d);
    }

private:
    int L_ = 0;
    int pentCount_ = 0;
    std::vector<QVector3D> dualVerts_;                  // size == ico.F.size(); vertex per primal triangle
    std::vector<Cell> cells_;
    std::vector<std::pair<int, int>> wireEdges_;         // unique undirected pairs of dual vertex indices
    std::vector<PickTri> pickTris_;                     // triangles for picking and green fill
    std::vector<std::array<int, 3>> dualOwners_; // для каждой дуальной вершины dv ? {cellA,cellB,cellC}
};

// Определение TreePlacement::getPosition
inline QVector3D TreePlacement::getPosition(const HexSphereModel& model) const {
    if (cellId < 0 || cellId >= static_cast<int>(model.cells().size())) {
        return QVector3D(0, 0, 0);
    }

    const auto& cell = model.cells()[cellId];
    if (triangleIdx < 0 || triangleIdx >= static_cast<int>(cell.poly.size())) {
        return cell.centroid;
    }

    int nextIdx = (triangleIdx + 1) % cell.poly.size();
    QVector3D v0 = model.dualVerts()[cell.poly[triangleIdx]];
    QVector3D v1 = model.dualVerts()[cell.poly[nextIdx]];
    QVector3D v2 = cell.centroid;

    return (v0 * baryU + v1 * baryV + v2 * baryW).normalized();
}