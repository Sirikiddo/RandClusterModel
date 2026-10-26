#pragma once

#include <QVector3D>

#include <cstdint>
#include <optional>
#include <vector>

#include "renderers/TerrainTessellator.h"

class HexSphereModel;

struct ShoreArc {
    QVector3D a;
    QVector3D b;
    QVector3D normal;
    QVector3D midpoint;
    float length = 0.0f;
    float halfLength = 0.0f;
};

struct ShoreDistanceQueryStats {
    std::uint64_t nodeVisits = 0;
    std::uint64_t exactArcTests = 0;
};

std::optional<ShoreArc> makeShoreArc(const QVector3D& a, const QVector3D& b);
float angularDistanceToShoreArc(const QVector3D& direction, const ShoreArc& arc);
float bruteForceNearestShoreDistance(
    const QVector3D& direction,
    const std::vector<ShoreArc>& arcs,
    ShoreDistanceQueryStats* stats = nullptr);
std::vector<ShoreArc> buildShoreArcs(const HexSphereModel& model);

class ShoreDistanceIndex {
public:
    explicit ShoreDistanceIndex(std::vector<ShoreArc> arcs);

    float nearestAngularDistance(
        const QVector3D& direction,
        ShoreDistanceQueryStats* stats = nullptr) const;

    const std::vector<ShoreArc>& arcs() const { return arcs_; }
    std::size_t nodeCount() const { return nodes_.size(); }

private:
    struct Node {
        QVector3D center;
        float radius = 0.0f;
        std::uint32_t begin = 0;
        std::uint32_t count = 0;
        int left = -1;
        int right = -1;

        bool isLeaf() const { return left < 0; }
    };

    int buildNode(std::uint32_t begin, std::uint32_t count);
    float lowerBound(const QVector3D& directionUnit, const Node& node) const;
    void queryNode(
        int nodeIndex,
        const QVector3D& directionUnit,
        float& best,
        ShoreDistanceQueryStats& stats) const;

    std::vector<ShoreArc> arcs_;
    std::vector<Node> nodes_;
    int root_ = -1;
};

enum class ShoreDistanceSearchMode {
    SpatialIndex,
    BruteForce,
};

enum class ShoreDistanceExecution {
    Serial,
    ParallelAuto,
};

struct SurfaceAtlasBuildOptions {
    ShoreDistanceSearchMode searchMode = ShoreDistanceSearchMode::SpatialIndex;
    ShoreDistanceExecution execution = ShoreDistanceExecution::ParallelAuto;
};

struct SurfaceAtlasBuildStats {
    int keptTriangles = 0;
    int culledTriangles = 0;
    int shorelineArcCount = 0;
    float maximumSeaDepth = 0.0f;
    std::uint64_t atlasVertices = 0;
    std::uint64_t uniqueDirections = 0;
    std::uint64_t cacheHits = 0;
    std::uint64_t bvhNodes = 0;
    std::uint64_t bvhNodeVisits = 0;
    std::uint64_t candidateArcs = 0;
    std::uint64_t exactDistanceTests = 0;
    std::uint64_t bruteForceTests = 0;
    int workers = 1;
    double shorelineMs = 0.0;
    double indexBuildMs = 0.0;
    double distanceQueryMs = 0.0;
};

struct SurfaceAtlasMeshData {
    std::vector<float> positions;
    std::vector<float> kinds;
    std::vector<float> shoreDistances;
    SurfaceAtlasBuildStats stats;
};

class SurfaceAtlasMeshBuilder {
public:
    static SurfaceAtlasMeshData build(
        const TerrainMesh& mesh,
        const HexSphereModel& model,
        const SurfaceAtlasBuildOptions& options = {});
};
