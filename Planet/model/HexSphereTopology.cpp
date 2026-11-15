#include "model/HexSphereTopology.h"

#include <mutex>
#include <deque>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include "model/IcosahedralGrid.h"

#include "model/HexSphereModel.h"

namespace {
// Keep a small bounded LRU.  A level switch should not force reconstruction
// when the user returns to a recently used level.
std::mutex g_topologyCacheMutex;
using GridKey = std::pair<int, SphereGeometry>;
std::deque<std::pair<GridKey, HexSphereTopologyPtr>> g_topologyCache;
constexpr size_t kTopologyCacheCapacity = 4;

HexSphereTopologyPtr buildGlobalTopology(int level) {
    // The application uses 32-bit indices and materializes the whole sphere.
    // The implicit grid itself supports much larger frequencies.
    if (level < 0 || level > 7) throw std::invalid_argument("Materialized grid level must be 0..7");
    const int n = 1 << level;
    const IcosahedralGrid grid(n);
    auto result = std::make_shared<HexSphereTopology>();
    result->geometry = SphereGeometry::GlobalVoronoi;
    result->subdivisionLevel = level;
    result->cells.resize(static_cast<size_t>(grid.cellCount()));
    result->dualVerts.resize(20u * n * n);
    result->dualOwners.resize(20u * n * n);
    result->wireEdges.resize(30u * n * n);
    result->pickTris.resize(60u * n * n);
    struct Hash {
        size_t operator()(const std::array<int, 3>& key) const noexcept {
            size_t hash = 0;
            for (int v : key) hash = hash * 16777619u ^ static_cast<size_t>(v);
            return hash;
        }
    };
    std::unordered_map<std::array<int, 3>, int, Hash> vertices;
    vertices.reserve(result->dualVerts.size());
    const auto vec = [](IcosahedralGrid::Point p) { return QVector3D(float(p.x), float(p.y), float(p.z)); };
    for (size_t id = 0; id < result->cells.size(); ++id) {
        const auto source = grid.cell(grid.address(id));
        auto& cell = result->cells[id];
        cell.id = static_cast<int>(id);
        cell.centroid = vec(source.generator);
        cell.isPentagon = source.degree == 5;
        result->pentagonCount += cell.isPentagon ? 1 : 0;
        cell.poly.reserve(source.degree);
        cell.neighbors.reserve(source.degree);
        for (int k = 0; k < source.degree; ++k) {
            const int neighbor = static_cast<int>(grid.id(source.neighbors[k]));
            std::array<int, 3> owners{cell.id, neighbor,
                static_cast<int>(grid.id(source.neighbors[(k + source.degree - 1) % source.degree]))};
            std::sort(owners.begin(), owners.end());
            const auto [it, inserted] = vertices.try_emplace(owners, static_cast<int>(vertices.size()));
            const size_t vertex = static_cast<size_t>(it->second);
            if (inserted) {
                if (vertex >= result->dualVerts.size()) throw std::logic_error("Too many dual vertices");
                result->dualVerts[vertex] = vec(grid.dualVertex(grid.address(owners[0]), grid.address(owners[1]), grid.address(owners[2])));
                result->dualOwners[vertex] = owners;
            }
            cell.poly.push_back(static_cast<int>(vertex));
            cell.neighbors.push_back(neighbor);
        }
    }
    size_t edge = 0, triangle = 0;
    for (const auto& cell : result->cells) {
        for (size_t k = 0; k < cell.poly.size(); ++k) {
            const int a = cell.poly[k], b = cell.poly[(k + 1) % cell.poly.size()];
            if (cell.id < cell.neighbors[k]) result->wireEdges.at(edge++) = {a, b};
            result->pickTris.at(triangle++) = {cell.id, cell.centroid, result->dualVerts[a], result->dualVerts[b]};
        }
    }
    if (vertices.size() != result->dualVerts.size() || edge != result->wireEdges.size()
        || triangle != result->pickTris.size() || result->pentagonCount != 12)
        throw std::logic_error("Invalid materialized global topology");
    return result;
}

HexSphereTopologyPtr buildTopologyUncached(int subdivisionLevel) {
    HexSphereModel builderModel;
    IcosphereBuilder builder;
    builderModel.rebuildFromIcosphere(builder.build(subdivisionLevel), subdivisionLevel);
    // rebuildFromIcosphere already materializes the immutable topology.
    return builderModel.topology();
}
} // namespace

HexSphereTopologyPtr buildHexSphereTopology(int subdivisionLevel, SphereGeometry geometry) {
    if (!validSphereGeometry(static_cast<int>(geometry))) throw std::invalid_argument("Invalid sphere geometry");
    const GridKey key{subdivisionLevel, geometry};
    std::lock_guard<std::mutex> lock(g_topologyCacheMutex);
    for (auto it = g_topologyCache.begin(); it != g_topologyCache.end(); ++it) {
        if (it->first == key) {
            auto result = *it;
            g_topologyCache.erase(it);
            g_topologyCache.push_back(result);
            return result.second;
        }
    }
    auto result = geometry == SphereGeometry::GlobalVoronoi
        ? buildGlobalTopology(subdivisionLevel) : buildTopologyUncached(subdivisionLevel);
    g_topologyCache.emplace_back(key, result);
    if (g_topologyCache.size() > kTopologyCacheCapacity) {
        g_topologyCache.pop_front();
    }
    return result;
}
