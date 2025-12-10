#pragma once
#include <QVector3D>
#include <QMatrix4x4>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <limits>

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

struct OreVisualParams {
    float density = 0.0f;           // Плотность [0-1]
    float grainSize = 0.05f;        // Размер зерна (0.01-0.1)
    float grainContrast = 1.0f;     // Контрастность зерен (1-3)
    QVector3D baseColor;            // Базовый цвет руды
    QVector3D grainColor;           // Цвет зерен
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

    // Новые атрибуты для климатической карты
    float temperature = 0.0f;    // температура [0..1]
    float humidity = 0.0f;       // влажность [0..1] 
    float pressure = 0.0f;       // давление [0..1]

    // Дополнительные характеристики
    float oreDensity = 0.0f;     // плотность руды [0..1]
    uint8_t oreType = 0;         // тип руды (0-нет, 1-железо, 2-медь, и т.д.)

    OreVisualParams oreVisual;      // Визуальные параметры руды
    float oreNoiseOffset = 0.0f;    // Смещение для анимации шума
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

    int subdivisions() const { return L_; }
    int pentagonCount() const { return pentCount_; }
    int cellCount() const { return static_cast<int>(cells_.size()); }

    // Удобные сеттеры
    void setHeight(int cellId, int h);
    void addHeight(int cellId, int dh);
    void setBiome(int cellId, Biome b);

    // Утилиты
    static QVector3D biomeColor(Biome b, float temperature = 0.5f) {
        // Базовые цвета биомов
        QVector3D baseColor;
        switch (b) {
        case Biome::Sea:      baseColor = { 0.12f, 0.40f, 0.85f }; break;
        case Biome::Grass:    baseColor = { 0.20f, 0.75f, 0.30f }; break;
        case Biome::Rock:     baseColor = { 0.60f, 0.60f, 0.60f }; break;
        case Biome::Snow:     baseColor = { 0.95f, 0.95f, 0.98f }; break;
        case Biome::Tundra:   baseColor = { 0.70f, 0.75f, 0.65f }; break;
        case Biome::Desert:   baseColor = { 0.90f, 0.85f, 0.55f }; break;
        case Biome::Savanna:  baseColor = { 0.75f, 0.70f, 0.30f }; break;
        case Biome::Jungle:   baseColor = { 0.15f, 0.55f, 0.20f }; break;
        default:              baseColor = { 1,1,1 }; break;
        }

        // Корректировка цвета в зависимости от температуры
        // Холодные температуры добавляют синий оттенок, теплые - красный
        QVector3D tempAdjust;
        if (temperature < 0.3f) {
            // Холодно - синий оттенок
            float coldFactor = (0.3f - temperature) / 0.3f;
            tempAdjust = { 0.0f, 0.0f, 0.2f * coldFactor };
        }
        else if (temperature > 0.7f) {
            // Жарко - красный/желтый оттенок
            float heatFactor = (temperature - 0.7f) / 0.3f;
            tempAdjust = { 0.15f * heatFactor, 0.1f * heatFactor, 0.0f };
        }

        return baseColor + tempAdjust;
    }

    // Утилиты для работы с климатическими данными
    float getAverageTemperature() const {
        float sum = 0.0f;
        for (const auto& cell : cells_) sum += cell.temperature;
        return cells_.empty() ? 0.0f : sum / cells_.size();
    }

    float getAverageHumidity() const {
        float sum = 0.0f;
        for (const auto& cell : cells_) sum += cell.humidity;
        return cells_.empty() ? 0.0f : sum / cells_.size();
    }

    // Получение ячеек с определенным типом руды
    std::vector<int> getCellsWithOre(uint8_t oreType) const {
        std::vector<int> result;
        for (const auto& cell : cells_) {
            if (cell.oreType == oreType && cell.oreDensity > 0.1f) {
                result.push_back(cell.id);
            }
        }
        return result;
    }

    // Сброс всех климатических данных
    void resetClimateData() {
        for (auto& cell : cells_) {
            cell.temperature = 0.0f;
            cell.humidity = 0.0f;
            cell.pressure = 0.0f;
            cell.oreDensity = 0.0f;
            cell.oreType = 0;
        }
    }

    void setTemperature(int cellId, float temp) {
        if (cellId >= 0 && cellId < (int)cells_.size()) {
            cells_[cellId].temperature = temp;
        }
    }

    void setHumidity(int cellId, float humidity) {
        if (cellId >= 0 && cellId < (int)cells_.size()) {
            cells_[cellId].humidity = humidity;
        }
    }

    void setPressure(int cellId, float pressure) {
        if (cellId >= 0 && cellId < (int)cells_.size()) {
            cells_[cellId].pressure = pressure;
        }
    }

    void setOreDensity(int cellId, float oreDensity) {
        if (cellId >= 0 && cellId < (int)cells_.size()) {
            cells_[cellId].oreDensity = oreDensity;
        }
    }

    void setOreType(int cellId, uint8_t oreType) {
        if (cellId >= 0 && cellId < (int)cells_.size()) {
            cells_[cellId].oreType = oreType;
        }
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