#include "renderers/SurfaceAtlasMeshBuilder.h"

#include <QElapsedTimer>
#include <QFuture>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include "model/HexSphereModel.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCapInflation = 1.0e-5f;
constexpr std::uint32_t kLeafSize = 8;
constexpr std::size_t kParallelThreshold = 4096;

float clampedAcos(float value) {
    // Shore distance queries call this millions of times at L6/L7. This
    // minimax-style approximation keeps the angular error below roughly
    // 1e-3 radians while avoiding a library acos call in the hot loop.
    const float x = std::clamp(value, -1.0f, 1.0f);
    const float ax = std::abs(x);
    float result = (((-0.0187293f * ax + 0.0742610f) * ax - 0.2121144f) * ax + 1.5707288f)
        * std::sqrt(std::max(0.0f, 1.0f - ax));
    if (x < 0.0f) result = kPi - result;
    return result;
}

QVector3D loadVec3(const std::vector<float>& data, std::uint32_t index) {
    const std::size_t base = static_cast<std::size_t>(index) * 3u;
    return QVector3D(data[base], data[base + 1u], data[base + 2u]);
}

float classifySurfaceKind(const HexSphereModel& model, int cellId) {
    if (cellId < 0 || cellId >= static_cast<int>(model.cells().size())) {
        return 0.0f;
    }
    return model.cells()[static_cast<std::size_t>(cellId)].biome == Biome::Sea ? 3.0f : 1.0f;
}

struct PositionKey {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;

    bool operator==(const PositionKey&) const = default;
};

struct PositionKeyHash {
    std::size_t operator()(const PositionKey& key) const noexcept {
        std::size_t hash = key.x;
        hash ^= static_cast<std::size_t>(key.y) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<std::size_t>(key.z) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        return hash;
    }
};

PositionKey exactPositionKey(const QVector3D& value) {
    // Geometry must never be snapped to a coarse grid: doing so collapses
    // distinct cap vertices and produces jagged coastlines. Shared cap vertices
    // are produced by identical math, so their bit patterns already match.
    auto bits = [](float component) -> std::uint32_t {
        return std::bit_cast<std::uint32_t>(component + 0.0f);
    };
    return { bits(value.x()), bits(value.y()), bits(value.z()) };
}

struct DistanceWorkItem {
    QVector3D position;
    float distance = kPi;
    float kind = 1.0f;
    float sign = -1.0f;
    // False for vertices whose cell is far enough from the coast that the
    // shore fade is already saturated; those skip the BVH entirely.
    bool exact = true;
    ShoreDistanceQueryStats stats;
};

// Open-addressing table for position deduplication. The old unordered_map
// allocated a node per unique direction and rehashed millions of buckets.
class PositionTable {
public:
    explicit PositionTable(std::size_t expected) {
        std::size_t capacity = 64;
        const std::size_t wanted = std::max<std::size_t>(expected * 2u, 64u);
        while (capacity < wanted) {
            capacity <<= 1;
        }
        slots_.assign(capacity, Slot{});
        mask_ = capacity - 1u;
    }

    // Returns (unique index, inserted).
    std::pair<std::uint32_t, bool> findOrInsert(const PositionKey& key, std::uint32_t nextIndex) {
        if ((used_ + 1u) * 10u >= slots_.size() * 7u) {
            grow();
        }
        std::size_t slot = PositionKeyHash{}(key) & mask_;
        while (slots_[slot].valuePlusOne != 0u) {
            if (slots_[slot].key == key) {
                return { slots_[slot].valuePlusOne - 1u, false };
            }
            slot = (slot + 1u) & mask_;
        }
        slots_[slot].key = key;
        slots_[slot].valuePlusOne = nextIndex + 1u;
        ++used_;
        return { nextIndex, true };
    }

private:
    struct Slot {
        PositionKey key;
        std::uint32_t valuePlusOne = 0u;
    };

    void grow() {
        std::vector<Slot> old = std::move(slots_);
        slots_.assign(old.size() * 2u, Slot{});
        mask_ = slots_.size() - 1u;
        used_ = 0;
        for (const Slot& slot : old) {
            if (slot.valuePlusOne == 0u) {
                continue;
            }
            std::size_t pos = PositionKeyHash{}(slot.key) & mask_;
            while (slots_[pos].valuePlusOne != 0u) {
                pos = (pos + 1u) & mask_;
            }
            slots_[pos] = slot;
            ++used_;
        }
    }

    std::vector<Slot> slots_;
    std::size_t mask_ = 0;
    std::size_t used_ = 0;
};

float angularDistanceToShoreArcUnit(const QVector3D& p, const ShoreArc& arc) {
    QVector3D projected = p - arc.normal * QVector3D::dotProduct(p, arc.normal);
    if (!projected.isNull()) {
        projected.normalize();
        if (QVector3D::dotProduct(projected, p) < 0.0f) {
            projected = -projected;
        }
        const float aToProjection = clampedAcos(QVector3D::dotProduct(arc.a, projected));
        const float projectionToB = clampedAcos(QVector3D::dotProduct(projected, arc.b));
        if (aToProjection + projectionToB <= arc.length + 2.0e-4f) {
            return clampedAcos(QVector3D::dotProduct(p, projected));
        }
    }
    return std::min(
        clampedAcos(QVector3D::dotProduct(p, arc.a)),
        clampedAcos(QVector3D::dotProduct(p, arc.b)));
}

} // namespace

std::optional<ShoreArc> makeShoreArc(const QVector3D& start, const QVector3D& end) {
    const QVector3D a = start.normalized();
    const QVector3D b = end.normalized();
    if (a.isNull() || b.isNull()) {
        return std::nullopt;
    }

    ShoreArc arc;
    arc.a = a;
    arc.b = b;
    arc.normal = QVector3D::crossProduct(a, b).normalized();
    arc.length = clampedAcos(QVector3D::dotProduct(a, b));
    if (arc.normal.isNull() || arc.length <= 1.0e-6f || arc.length >= kPi - 1.0e-6f) {
        return std::nullopt;
    }
    arc.midpoint = (a + b).normalized();
    if (arc.midpoint.isNull()) {
        return std::nullopt;
    }
    arc.halfLength = arc.length * 0.5f;
    return arc;
}

float angularDistanceToShoreArc(const QVector3D& direction, const ShoreArc& arc) {
    const QVector3D p = direction.normalized();
    return angularDistanceToShoreArcUnit(p, arc);
}

float bruteForceNearestShoreDistance(
    const QVector3D& direction,
    const std::vector<ShoreArc>& arcs,
    ShoreDistanceQueryStats* stats) {
    float best = kPi;
    for (const ShoreArc& arc : arcs) {
        best = std::min(best, angularDistanceToShoreArc(direction, arc));
    }
    if (stats) {
        stats->exactArcTests += arcs.size();
    }
    return best;
}

std::vector<ShoreArc> buildShoreArcs(const HexSphereModel& model) {
    std::vector<ShoreArc> arcs;
    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();
    for (const Cell& cell : cells) {
        const std::size_t edgeCount = std::min(cell.poly.size(), cell.neighbors.size());
        for (std::size_t edge = 0; edge < edgeCount; ++edge) {
            const int neighborId = cell.neighbors[edge];
            if (neighborId < 0 || neighborId >= static_cast<int>(cells.size()) || cell.id > neighborId) {
                continue;
            }
            if ((cell.biome == Biome::Sea) == (cells[static_cast<std::size_t>(neighborId)].biome == Biome::Sea)) {
                continue;
            }
            const auto arc = makeShoreArc(
                dual[static_cast<std::size_t>(cell.poly[edge])],
                dual[static_cast<std::size_t>(cell.poly[(edge + 1u) % cell.poly.size()])]);
            if (arc) {
                arcs.push_back(*arc);
            }
        }
    }
    return arcs;
}

TerrainMesh buildAtlasSourceMesh(const HexSphereModel& model) {
    TerrainMesh mesh;
    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();

    std::size_t triangleCount = 0;
    for (const Cell& cell : cells) {
        if (cell.poly.size() >= 3) {
            triangleCount += cell.poly.size();
        }
    }
    mesh.pos.reserve(triangleCount * 9u);
    mesh.idx.reserve(triangleCount * 3u);
    mesh.triOwner.reserve(triangleCount);
    mesh.triSurfaceRole.reserve(triangleCount);

    for (const Cell& cell : cells) {
        const int degree = static_cast<int>(cell.poly.size());
        if (degree < 3) {
            continue;
        }
        const float height = static_cast<float>(cell.height);
        const QVector3D center = model.positionOnSurface(cell.centroid, height);
        for (int i = 0; i < degree; ++i) {
            const int a = cell.poly[static_cast<std::size_t>(i)];
            const int b = cell.poly[static_cast<std::size_t>((i + 1) % degree)];
            if (a < 0 || b < 0
                || static_cast<std::size_t>(a) >= dual.size()
                || static_cast<std::size_t>(b) >= dual.size()) {
                continue;
            }
            const QVector3D pa = model.positionOnSurface(dual[static_cast<std::size_t>(a)], height);
            const QVector3D pb = model.positionOnSurface(dual[static_cast<std::size_t>(b)], height);
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.pos.size() / 3u);
            mesh.pos.insert(mesh.pos.end(), {
                center.x(), center.y(), center.z(),
                pa.x(), pa.y(), pa.z(),
                pb.x(), pb.y(), pb.z() });
            mesh.idx.insert(mesh.idx.end(), { base, base + 1u, base + 2u });
            mesh.triOwner.push_back(cell.id);
            mesh.triSurfaceRole.push_back(TriangleSurfaceRole::Top);
        }
    }
    return mesh;
}

ShoreDistanceIndex::ShoreDistanceIndex(std::vector<ShoreArc> arcs)
    : arcs_(std::move(arcs)) {
    nodes_.reserve(arcs_.empty() ? 0u : arcs_.size() * 2u);
    if (!arcs_.empty()) {
        root_ = buildNode(0u, static_cast<std::uint32_t>(arcs_.size()));
    }
}

int ShoreDistanceIndex::buildNode(std::uint32_t begin, std::uint32_t count) {
    const int nodeIndex = static_cast<int>(nodes_.size());
    nodes_.push_back({});

    QVector3D center;
    QVector3D minimum(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    QVector3D maximum(
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest());
    for (std::uint32_t i = begin; i < begin + count; ++i) {
        center += arcs_[i].midpoint;
        const QVector3D& midpoint = arcs_[i].midpoint;
        minimum.setX(std::min(minimum.x(), midpoint.x()));
        minimum.setY(std::min(minimum.y(), midpoint.y()));
        minimum.setZ(std::min(minimum.z(), midpoint.z()));
        maximum.setX(std::max(maximum.x(), midpoint.x()));
        maximum.setY(std::max(maximum.y(), midpoint.y()));
        maximum.setZ(std::max(maximum.z(), midpoint.z()));
    }
    center = center.normalized();
    if (center.isNull()) {
        center = arcs_[begin].midpoint;
    }

    float radius = 0.0f;
    for (std::uint32_t i = begin; i < begin + count; ++i) {
        radius = std::max(
            radius,
            clampedAcos(QVector3D::dotProduct(center, arcs_[i].midpoint)) + arcs_[i].halfLength);
    }

    Node& node = nodes_[static_cast<std::size_t>(nodeIndex)];
    node.center = center;
    node.radius = std::min(kPi, radius + kCapInflation);
    node.begin = begin;
    node.count = count;

    if (count <= kLeafSize) {
        return nodeIndex;
    }

    const QVector3D extent = maximum - minimum;
    int axis = 0;
    if (extent.y() > extent.x()) axis = 1;
    if (extent.z() > (axis == 0 ? extent.x() : extent.y())) axis = 2;
    const std::uint32_t middle = begin + count / 2u;
    auto coordinate = [axis](const ShoreArc& arc) {
        if (axis == 0) return arc.midpoint.x();
        if (axis == 1) return arc.midpoint.y();
        return arc.midpoint.z();
    };
    std::nth_element(
        arcs_.begin() + begin,
        arcs_.begin() + middle,
        arcs_.begin() + begin + count,
        [&](const ShoreArc& lhs, const ShoreArc& rhs) { return coordinate(lhs) < coordinate(rhs); });

    const int left = buildNode(begin, middle - begin);
    const int right = buildNode(middle, begin + count - middle);
    nodes_[static_cast<std::size_t>(nodeIndex)].left = left;
    nodes_[static_cast<std::size_t>(nodeIndex)].right = right;
    return nodeIndex;
}

float ShoreDistanceIndex::lowerBound(const QVector3D& directionUnit, const Node& node) const {
    return std::max(0.0f, clampedAcos(QVector3D::dotProduct(directionUnit, node.center)) - node.radius);
}

void ShoreDistanceIndex::queryNode(
    int nodeIndex,
    const QVector3D& directionUnit,
    float& best,
    ShoreDistanceQueryStats& stats) const {
    const Node& node = nodes_[static_cast<std::size_t>(nodeIndex)];
    ++stats.nodeVisits;
    if (lowerBound(directionUnit, node) > best) {
        return;
    }

    if (node.isLeaf()) {
        for (std::uint32_t i = node.begin; i < node.begin + node.count; ++i) {
            best = std::min(best, angularDistanceToShoreArcUnit(directionUnit, arcs_[i]));
            ++stats.exactArcTests;
        }
        return;
    }

    const float leftBound = lowerBound(directionUnit, nodes_[static_cast<std::size_t>(node.left)]);
    const float rightBound = lowerBound(directionUnit, nodes_[static_cast<std::size_t>(node.right)]);
    const int first = leftBound <= rightBound ? node.left : node.right;
    const int second = leftBound <= rightBound ? node.right : node.left;
    queryNode(first, directionUnit, best, stats);
    queryNode(second, directionUnit, best, stats);
}

float ShoreDistanceIndex::nearestAngularDistance(
    const QVector3D& direction,
    ShoreDistanceQueryStats* stats) const {
    ShoreDistanceQueryStats localStats;
    if (root_ < 0) {
        if (stats) *stats = localStats;
        return kPi;
    }
    const QVector3D directionUnit = direction.normalized();
    float best = kPi;
    queryNode(root_, directionUnit, best, localStats);
    if (stats) *stats = localStats;
    return best;
}

bool ShoreDistanceIndex::hasArcWithin(
    const QVector3D& direction,
    float angularRadius) const {
    if (root_ < 0) {
        return false;
    }
    return anyWithin(root_, direction.normalized(), angularRadius);
}

bool ShoreDistanceIndex::anyWithin(
    int nodeIndex,
    const QVector3D& directionUnit,
    float angularRadius) const {
    const Node& node = nodes_[static_cast<std::size_t>(nodeIndex)];
    if (lowerBound(directionUnit, node) > angularRadius) {
        return false;
    }
    if (node.isLeaf()) {
        for (std::uint32_t i = node.begin; i < node.begin + node.count; ++i) {
            if (angularDistanceToShoreArcUnit(directionUnit, arcs_[i]) <= angularRadius) {
                return true;
            }
        }
        return false;
    }
    return anyWithin(node.left, directionUnit, angularRadius)
        || anyWithin(node.right, directionUnit, angularRadius);
}

SurfaceAtlasMeshData SurfaceAtlasMeshBuilder::build(
    const TerrainMesh& mesh,
    const HexSphereModel& model,
    const SurfaceAtlasBuildOptions& options) {
    SurfaceAtlasMeshData result;

    QElapsedTimer timer;
    timer.start();
    std::vector<ShoreArc> shoreline = buildShoreArcs(model);
    result.stats.shorelineMs = timer.nsecsElapsed() / 1000000.0;
    result.stats.shorelineArcCount = static_cast<int>(shoreline.size());

    const float waterRadius = model.waterSurfaceRadius();
    for (const Cell& cell : model.cells()) {
        if (cell.biome == Biome::Sea) {
            result.stats.maximumSeaDepth = std::max(
                result.stats.maximumSeaDepth,
                waterRadius - model.radiusForHeight(static_cast<float>(cell.height)));
        }
    }

    timer.restart();
    ShoreDistanceIndex index(shoreline);
    result.stats.indexBuildMs = timer.nsecsElapsed() / 1000000.0;
    result.stats.bvhNodes = index.nodeCount();

    const std::size_t uniqueEstimate = std::max<std::size_t>(16u, mesh.idx.size() / 3u);
    PositionTable uniqueLookup(uniqueEstimate);
    std::vector<DistanceWorkItem> workItems;
    workItems.reserve(uniqueEstimate);
    result.positions.reserve(uniqueEstimate * 3u);
    result.kinds.reserve(uniqueEstimate);
    result.shoreDistances.reserve(uniqueEstimate);
    // One index per emitted triangle corner; the vertex arrays stay per-unique.
    result.indices.reserve(mesh.idx.size());

    // The water shader fades waves over roughly a tenth of a cell, so any
    // shoreline distance beyond half a cell only has to stay "far enough".
    // Mark cells whose caps can still contain such near-shore vertices and let
    // the rest use a constant, skipping their BVH queries.
    const std::size_t cellCount = model.cells().size();
    const float angularCellWidth = cellCount > 0
        ? std::sqrt(4.0f * kPi / static_cast<float>(cellCount))
        : 0.0f;
    const float clampAngularDistance = 0.5f * angularCellWidth;
    const float cellRadiusAngular = 0.8f * angularCellWidth;
    std::vector<char> cellNearShore(cellCount, 0);
    if (!shoreline.empty() && cellCount > 0) {
        QElapsedTimer gateTimer;
        gateTimer.start();
        const float exactRadius = clampAngularDistance + cellRadiusAngular;
        for (std::size_t i = 0; i < cellCount; ++i) {
            cellNearShore[i] = index.hasArcWithin(
                model.cells()[i].centroid, exactRadius) ? 1 : 0;
        }
        result.stats.shoreGateMs = gateTimer.nsecsElapsed() / 1000000.0;
    }

    const std::size_t triangleCount = mesh.idx.size() / 3u;
    for (std::size_t tri = 0; tri < triangleCount; ++tri) {
        const TriangleSurfaceRole role = tri < mesh.triSurfaceRole.size()
            ? mesh.triSurfaceRole[tri]
            : TriangleSurfaceRole::Cliff;
        if (role != TriangleSurfaceRole::Top) {
            ++result.stats.culledTriangles;
            continue;
        }

        const int owner = tri < mesh.triOwner.size() ? mesh.triOwner[tri] : -1;
        const float surfaceKind = classifySurfaceKind(model, owner);
        const float shoreSign = surfaceKind == 3.0f ? 1.0f : -1.0f;
        const bool ownerNearShore = owner >= 0
            && static_cast<std::size_t>(owner) < cellNearShore.size()
            && cellNearShore[static_cast<std::size_t>(owner)] != 0;
        for (std::size_t corner = 0; corner < 3u; ++corner) {
            const std::uint32_t vertexIndex = mesh.idx[tri * 3u + corner];
            const QVector3D position = loadVec3(mesh.pos, vertexIndex);

            const PositionKey key = exactPositionKey(position);
            const auto [uniqueIndex, inserted] = uniqueLookup.findOrInsert(
                key, static_cast<std::uint32_t>(workItems.size()));
            if (inserted) {
                DistanceWorkItem item;
                item.position = position;
                item.kind = surfaceKind;
                item.sign = shoreSign;
                item.exact = ownerNearShore;
                workItems.push_back(item);
            }
            else if (ownerNearShore) {
                // Shared vertex first seen from a far cell but owned by a near
                // cell elsewhere: it still needs an exact distance.
                workItems[uniqueIndex].exact = true;
            }
            result.indices.push_back(uniqueIndex);
        }
        ++result.stats.keptTriangles;
    }

    result.stats.atlasVertices = result.indices.size();
    result.stats.uniqueDirections = workItems.size();
    result.stats.cacheHits = result.stats.atlasVertices - result.stats.uniqueDirections;
    for (const DistanceWorkItem& item : workItems) {
        if (item.exact) {
            ++result.stats.exactDirections;
        }
    }
    result.stats.bruteForceTests = result.stats.uniqueDirections * shoreline.size();

    timer.restart();
    auto evaluateRange = [&](std::size_t begin, std::size_t end, ShoreDistanceQueryStats& workerStats) {
        for (std::size_t i = begin; i < end; ++i) {
            if (!workItems[i].exact) {
                workItems[i].distance = shoreline.empty() ? kPi : clampAngularDistance;
                continue;
            }
            ShoreDistanceQueryStats queryStats;
            if (options.searchMode == ShoreDistanceSearchMode::BruteForce) {
                workItems[i].distance = bruteForceNearestShoreDistance(workItems[i].position, shoreline, &queryStats);
            } else {
                workItems[i].distance = index.nearestAngularDistance(workItems[i].position, &queryStats);
            }
            workerStats.nodeVisits += queryStats.nodeVisits;
            workerStats.exactArcTests += queryStats.exactArcTests;
        }
    };

    int workerCount = 1;
    std::vector<ShoreDistanceQueryStats> workerStats;
    if (options.execution == ShoreDistanceExecution::ParallelAuto
        && workItems.size() >= kParallelThreshold) {
        QThreadPool* pool = QThreadPool::globalInstance();
        workerCount = std::max(1, std::min(
            pool->maxThreadCount(),
            static_cast<int>((workItems.size() + kParallelThreshold - 1u) / kParallelThreshold)));
    }
    workerStats.resize(static_cast<std::size_t>(workerCount));

    if (workerCount == 1) {
        evaluateRange(0u, workItems.size(), workerStats[0]);
    } else {
        std::vector<QFuture<void>> futures;
        futures.reserve(static_cast<std::size_t>(workerCount));
        for (int worker = 0; worker < workerCount; ++worker) {
            const std::size_t begin = workItems.size() * static_cast<std::size_t>(worker)
                / static_cast<std::size_t>(workerCount);
            const std::size_t end = workItems.size() * static_cast<std::size_t>(worker + 1)
                / static_cast<std::size_t>(workerCount);
            futures.push_back(QtConcurrent::run(
                QThreadPool::globalInstance(),
                [&, begin, end, worker]() {
                    evaluateRange(begin, end, workerStats[static_cast<std::size_t>(worker)]);
                }));
        }
        for (QFuture<void>& future : futures) {
            future.waitForFinished();
        }
    }

    result.stats.workers = workerCount;
    for (const ShoreDistanceQueryStats& stats : workerStats) {
        result.stats.bvhNodeVisits += stats.nodeVisits;
        result.stats.exactDistanceTests += stats.exactArcTests;
    }
    result.stats.candidateArcs = result.stats.exactDistanceTests;

    const std::size_t uniqueCount = workItems.size();
    result.positions.resize(uniqueCount * 3u);
    result.kinds.resize(uniqueCount);
    result.shoreDistances.resize(uniqueCount);
    for (std::size_t i = 0; i < uniqueCount; ++i) {
        const DistanceWorkItem& item = workItems[i];
        result.positions[i * 3u] = item.position.x();
        result.positions[i * 3u + 1u] = item.position.y();
        result.positions[i * 3u + 2u] = item.position.z();
        result.kinds[i] = item.kind;
        result.shoreDistances[i] = item.sign * item.distance * waterRadius;
    }
    result.stats.distanceQueryMs = timer.nsecsElapsed() / 1000000.0;
    return result;
}
