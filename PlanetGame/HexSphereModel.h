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

enum class Biome : uint8_t { Sea = 0, Grass = 1, Rock = 2 };

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
    const std::vector<PickTri>& pickTris() const { return pickTris_; }

    int subdivisions() const { return L_; }
    int pentagonCount() const { return pentCount_; }

    // Удобные сеттеры
    void setHeight(int cellId, int h);
    void addHeight(int cellId, int dh);
    void setBiome(int cellId, Biome b);

    // Утилиты
    static QVector3D biomeColor(Biome b)
    {
        switch (b) {
        case Biome::Sea:   return { 0.12f, 0.40f, 0.85f };
        case Biome::Grass: return { 0.20f, 0.75f, 0.30f };
        case Biome::Rock:  return { 0.60f, 0.60f, 0.60f };
        }
        return { 1,1,1 };
    }

    const std::vector<std::array<int, 3>>& dualOwners() const { return dualOwners_; }

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