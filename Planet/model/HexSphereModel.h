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

#include "model/HexSphereTopology.h"
#include <cstddef>

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

enum class OreType : uint8_t {
    None = 0,
    Iron = 1,
    Copper = 2,
    Gold = 3,
    Diamond = 4
};

// Forward declaration
class HexSphereModel;

// Structure for tree placement with random position within cell
struct TreePlacement {
    enum class PlacementMode : uint8_t {
        Surface = 0,
        World = 1
    };

    int cellId = -1;
    int triangleIdx = 0;
    float baryU = 0.33f;
    float baryV = 0.33f;
    float baryW = 0.34f;
    float scale = 1.0f;
    float rotation = 0.0f;

    // УБРАНО: TreeType treeType;
    // УБРАНО: TreeColorType colorType;
    // УБРАНО: foliageColor / trunkColor
    // (цвета теперь берутся из TreeBuilder::TreeVariant по species+variant)

    PlacementMode placementMode = PlacementMode::Surface;
    QVector3D worldPosition = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D worldUp = QVector3D(0.0f, 1.0f, 0.0f);
    float worldYaw = 0.0f;
    float worldScale = 1.0f;

    QVector3D getPosition(const HexSphereModel& model) const;

    void applyGlobalScale(float globalScale) { scale *= globalScale; }

    // Нужно, чтобы InputController понял, изменился ли набор размещений, пришедший
    // из DAG scene backend, и обновил блокировку дороги деревьями.
    bool operator==(const TreePlacement& other) const {
        return cellId == other.cellId
            && triangleIdx == other.triangleIdx
            && baryU == other.baryU
            && baryV == other.baryV
            && baryW == other.baryW
            && scale == other.scale
            && rotation == other.rotation
            && placementMode == other.placementMode
            && worldPosition == other.worldPosition
            && worldUp == other.worldUp
            && worldYaw == other.worldYaw
            && worldScale == other.worldScale;
    }
};
// Mutable simulation/render state for one logical cell. Kept separate from
// topology so terrain/ore updates do not change adjacency or geometry.
struct TerrainCellState {
    int height = 0;
    Biome biome = Biome::Grass;
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;
    float oreDensity = 0.0f;
    OreType oreType = OreType::None;
};

// Dual (hex/pent) sphere data
struct Cell : TopologyCell, TerrainCellState {
    uint32_t stateMask = 0;      // bit 0 => selected
};

class HexSphereModel {
public:
    static constexpr float kDefaultBaseRadius = 1.0f;
    static constexpr float kDefaultWaterSurfaceLevel = -0.5f;

    HexSphereModel() = default;

    void rebuildFromIcosphere(const IcoMesh& ico, int subdivisionLevel = 0);
    explicit HexSphereModel(HexSphereTopologyPtr topology);
    void setTopology(HexSphereTopologyPtr topology);

    HexSphereTopologyPtr topology() const noexcept { return topology_; }
    bool hasSharedTopology() const noexcept { return static_cast<bool>(topology_); }

    // Diagnostic counter for topology work. It counts actual dual/topology
    // reconstruction calls, not terrain-field updates.
    static std::size_t topologyBuildCount() noexcept;
    static void resetTopologyBuildCount() noexcept;

    const std::vector<QVector3D>& dualVerts() const {
        return topology_ ? topology_->dualVerts : dualVerts_;
    }
    const std::vector<std::pair<int, int>>& wireEdges() const {
        return topology_ ? topology_->wireEdges : wireEdges_;
    }
    const std::vector<Cell>& cells() const { return cells_; }
    std::vector<Cell>& cells() { return cells_; }
    const std::vector<PickTri>& pickTris() const {
        return topology_ ? topology_->pickTris : pickTris_;
    }
    const std::vector<std::array<int, 3>>& dualOwners() const {
        return topology_ ? topology_->dualOwners : dualOwners_;
    }

    int subdivisions() const { return L_; }
    int pentagonCount() const { return pentCount_; }
    int cellCount() const { return static_cast<int>(cells_.size()); }

    void setBaseRadius(float radius) { baseRadius_ = radius; }
    float baseRadius() const { return baseRadius_; }

    void setHeightStep(float step) { heightStep_ = step; }
    float heightStep() const { return heightStep_; }

    void setWaterSurfaceLevel(float level) { waterSurfaceLevel_ = level; }
    float waterSurfaceLevel() const { return waterSurfaceLevel_; }

    float radiusForHeight(float height) const;
    float radiusDeltaForHeightOffset(float deltaHeight) const;
    float waterSurfaceRadius() const;
    QVector3D positionOnSurface(const QVector3D& unitDir, float height, float bias = 0.0f) const;
    QVector3D cellSurfacePosition(int cellId, float bias = 0.0f) const;

    // Ð£Ð´Ð¾Ð±Ð½Ñ‹Ðµ ÑÐµÑ‚Ñ‚ÐµÑ€Ñ‹
    void setHeight(int cellId, int h);
    void addHeight(int cellId, int dh);
    void setBiome(int cellId, Biome b);

    // Ð¡ÐµÑ‚Ñ‚ÐµÑ€Ñ‹ Ð´Ð»Ñ ÐºÐ»Ð¸Ð¼Ð°Ñ‚Ð¸Ñ‡ÐµÑÐºÐ¸Ñ… Ð´Ð°Ð½Ð½Ñ‹Ñ…
    void setTemperature(int cellId, float temp);
    void setHumidity(int cellId, float humidity);
    void setPressure(int cellId, float pressure);
    void setOreDensity(int cellId, float oreDensity);
    void setOreType(int cellId, OreType oreType);

    // Ð£Ñ‚Ð¸Ð»Ð¸Ñ‚Ñ‹ Ð´Ð»Ñ ÐºÐ»Ð¸Ð¼Ð°Ñ‚Ð¸Ñ‡ÐµÑÐºÐ¸Ñ… Ð´Ð°Ð½Ð½Ñ‹Ñ…
    float getAverageTemperature() const;
    float getAverageHumidity() const;
    std::vector<int> getCellsWithOre(OreType oreType) const;
    void resetClimateData();

    // Ð£Ñ‚Ð¸Ð»Ð¸Ñ‚Ñ‹
    static QVector3D biomeColor(Biome b, float temperature = 0.5f);

    // Ð”Ð»Ñ Ñ‚ÐµÑÑ‚Ð¸Ñ€Ð¾Ð²Ð°Ð½Ð¸Ñ
    void debug_setCellsAndDual(std::vector<Cell> c, std::vector<QVector3D> d) {
        cells_ = std::move(c);
        dualVerts_ = std::move(d);
    }

    // Resets mutable terrain fields while keeping topology and geometry.
    void resetCellStates();
    void rebuildPickTris();

private:
    HexSphereTopologyPtr topology_;
    int L_ = 0;
    int pentCount_ = 0;
    float baseRadius_ = kDefaultBaseRadius;
    float heightStep_ = 0.05f;
    float waterSurfaceLevel_ = kDefaultWaterSurfaceLevel;
    std::vector<QVector3D> dualVerts_;                  // size == ico.F.size(); vertex per primal triangle
    std::vector<Cell> cells_;
    std::vector<std::pair<int, int>> wireEdges_;         // unique undirected pairs of dual vertex indices
    std::vector<PickTri> pickTris_;                     // triangles for picking and green fill
    std::vector<std::array<int, 3>> dualOwners_; // Ð´Ð»Ñ ÐºÐ°Ð¶Ð´Ð¾Ð¹ Ð´ÑƒÐ°Ð»ÑŒÐ½Ð¾Ð¹ Ð²ÐµÑ€ÑˆÐ¸Ð½Ñ‹ dv ? {cellA,cellB,cellC}
};

// ÐžÐ¿Ñ€ÐµÐ´ÐµÐ»ÐµÐ½Ð¸Ðµ TreePlacement::getPosition
inline QVector3D TreePlacement::getPosition(const HexSphereModel& model) const {
    if (placementMode == PlacementMode::World) {
        return worldPosition;
    }

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
