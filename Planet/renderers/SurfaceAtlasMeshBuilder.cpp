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
#include <unordered_map>

#include "model/HexSphereModel.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCapInflation = 1.0e-5f;
constexpr std::uint32_t kLeafSize = 8;
constexpr std::size_t kParallelThreshold = 4096;

float clampedAcos(float value) {
    return std::acos(std::clamp(value, -1.0f, 1.0f));
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

PositionKey positionKey(const QVector3D& value) {
    return {
        std::bit_cast<std::uint32_t>(value.x()),
        std::bit_cast<std::uint32_t>(value.y()),
        std::bit_cast<std::uint32_t>(value.z()),
    };
}

struct DistanceWorkItem {
    QVector3D position;
    float distance = kPi;
    ShoreDistanceQueryStats stats;
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

SurfaceAtlasMeshData SurfaceAtlasMeshBuilder::build(
    const TerrainMesh& mesh,
    const HexSphereModel& model,
    const SurfaceAtlasBuildOptions& options) {
    SurfaceAtlasMeshData result;
    result.positions.reserve(mesh.idx.size() * 3u);
    result.kinds.reserve(mesh.idx.size());
    result.shoreDistances.reserve(mesh.idx.size());

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

    std::unordered_map<PositionKey, std::uint32_t, PositionKeyHash> uniqueLookup;
    uniqueLookup.reserve(mesh.idx.size());
    std::vector<DistanceWorkItem> workItems;
    workItems.reserve(mesh.idx.size());
    std::vector<std::uint32_t> outputToUnique;
    outputToUnique.reserve(mesh.idx.size());
    std::vector<float> outputSigns;
    outputSigns.reserve(mesh.idx.size());

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
        for (std::size_t corner = 0; corner < 3u; ++corner) {
            const std::uint32_t vertexIndex = mesh.idx[tri * 3u + corner];
            const QVector3D position = loadVec3(mesh.pos, vertexIndex);
            result.positions.push_back(position.x());
            result.positions.push_back(position.y());
            result.positions.push_back(position.z());
            result.kinds.push_back(surfaceKind);
            outputSigns.push_back(shoreSign);

            const PositionKey key = positionKey(position);
            const auto [it, inserted] = uniqueLookup.try_emplace(key, static_cast<std::uint32_t>(workItems.size()));
            if (inserted) {
                workItems.push_back(DistanceWorkItem{ position });
            }
            outputToUnique.push_back(it->second);
        }
        ++result.stats.keptTriangles;
    }

    result.stats.atlasVertices = outputToUnique.size();
    result.stats.uniqueDirections = workItems.size();
    result.stats.cacheHits = result.stats.atlasVertices - result.stats.uniqueDirections;
    result.stats.bruteForceTests = result.stats.uniqueDirections * shoreline.size();

    timer.restart();
    auto evaluateRange = [&](std::size_t begin, std::size_t end, ShoreDistanceQueryStats& workerStats) {
        for (std::size_t i = begin; i < end; ++i) {
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

    result.shoreDistances.resize(outputToUnique.size());
    for (std::size_t i = 0; i < outputToUnique.size(); ++i) {
        result.shoreDistances[i] = outputSigns[i]
            * workItems[outputToUnique[i]].distance
            * waterRadius;
    }
    result.stats.distanceQueryMs = timer.nsecsElapsed() / 1000000.0;
    return result;
}
