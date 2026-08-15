#include "tests/SurfaceAtlasDistanceTests.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <stdexcept>

#include <QThreadPool>

#include "generation/MeshGenerators/TerrainMeshGenerator.h"
#include "model/HexSphereModel.h"
#include "renderers/SurfaceAtlasMeshBuilder.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool close(float lhs, float rhs, float epsilon = 1.0e-5f) {
    return std::abs(lhs - rhs) <= epsilon;
}

QVector3D unit(float x, float y, float z) {
    return QVector3D(x, y, z).normalized();
}

HexSphereModel buildModel(int level) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(level));
    return model;
}

void applyDeterministicCoast(HexSphereModel& model) {
    for (Cell& cell : model.cells()) {
        const std::uint32_t hash = static_cast<std::uint32_t>(cell.id) * 747796405u + 2891336453u;
        cell.biome = ((hash >> 29u) & 3u) == 0u ? Biome::Sea : Biome::Grass;
    }
}

void compareIndexWithBruteForce(
    const std::vector<ShoreArc>& arcs,
    const std::vector<QVector3D>& queries,
    float epsilon = 1.0e-5f) {
    ShoreDistanceIndex index(arcs);
    for (const QVector3D& query : queries) {
        const float expected = bruteForceNearestShoreDistance(query, arcs);
        const float actual = index.nearestAngularDistance(query);
        require(close(actual, expected, epsilon), "BVH result differs from brute-force shore distance");
    }
}

void testSingleArcGeometry() {
    const auto arc = makeShoreArc(unit(1.0f, 0.0f, 0.0f), unit(0.0f, 1.0f, 0.0f));
    require(arc.has_value(), "valid quarter-circle shore arc was rejected");
    const std::vector<ShoreArc> arcs{ *arc };
    const QVector3D midpoint = unit(1.0f, 1.0f, 0.0f);
    require(close(bruteForceNearestShoreDistance(arc->a, arcs), 0.0f), "arc endpoint distance is not zero");
    require(bruteForceNearestShoreDistance(midpoint, arcs) < 5.0e-4f, "arc midpoint distance is not near zero");
    require(close(bruteForceNearestShoreDistance(unit(0.0f, 0.0f, 1.0f), arcs), kPi * 0.5f),
        "distance perpendicular to arc plane is incorrect");
    compareIndexWithBruteForce(arcs, { arc->a, arc->b, midpoint, unit(-1.0f, 0.0f, 0.0f) });
}

void testEmptyDegenerateAndExtremeInputs() {
    ShoreDistanceIndex empty({});
    require(close(empty.nearestAngularDistance(unit(1.0f, 0.0f, 0.0f)), kPi),
        "empty shoreline must return pi");
    require(!makeShoreArc({}, unit(1.0f, 0.0f, 0.0f)).has_value(), "zero endpoint must be rejected");
    require(!makeShoreArc(unit(1.0f, 0.0f, 0.0f), unit(1.0f, 0.0f, 0.0f)).has_value(),
        "zero-length arc must be rejected");
    require(!makeShoreArc(unit(1.0f, 0.0f, 0.0f), unit(-1.0f, 0.0f, 0.0f)).has_value(),
        "antipodal arc must be rejected");

    std::vector<ShoreArc> arcs;
    const auto longArc = makeShoreArc(unit(1.0f, 0.0f, 0.0f), unit(-0.99f, 0.1f, 0.0f));
    const auto polarArc = makeShoreArc(unit(0.01f, 0.0f, 1.0f), unit(0.0f, 0.02f, 1.0f));
    require(longArc.has_value() && polarArc.has_value(), "valid extreme arc was rejected");
    arcs.push_back(*longArc);
    arcs.push_back(*polarArc);
    compareIndexWithBruteForce(arcs, {
        unit(0.0f, 0.0f, 1.0f), unit(0.0f, 0.0f, -1.0f),
        unit(1.0f, 1.0f, 0.0f), unit(-1.0f, -1.0f, 0.001f),
    });
}

void testDeterministicRandomOracle() {
    std::mt19937 rng(0x51A7u);
    std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
    std::vector<ShoreArc> arcs;
    while (arcs.size() < 256u) {
        const auto arc = makeShoreArc(
            unit(distribution(rng), distribution(rng), distribution(rng)),
            unit(distribution(rng), distribution(rng), distribution(rng)));
        if (arc) arcs.push_back(*arc);
    }

    std::vector<QVector3D> queries;
    queries.reserve(2048u);
    for (int i = 0; i < 2048; ++i) {
        queries.push_back(unit(distribution(rng), distribution(rng), distribution(rng)));
    }
    compareIndexWithBruteForce(arcs, queries);
}

void testActualTopologyOracle() {
    for (int level : { 2, 3 }) {
        HexSphereModel model = buildModel(level);
        applyDeterministicCoast(model);
        const std::vector<ShoreArc> arcs = buildShoreArcs(model);
        require(!arcs.empty(), "deterministic topology produced no shoreline");
        std::vector<QVector3D> queries;
        const std::size_t stride = std::max<std::size_t>(1u, model.dualVerts().size() / 512u);
        for (std::size_t i = 0; i < model.dualVerts().size(); i += stride) {
            queries.push_back(model.dualVerts()[i]);
        }
        compareIndexWithBruteForce(arcs, queries);
    }
}

TerrainMesh makeRepeatedMesh(const HexSphereModel& model, std::size_t triangleCount) {
    TerrainMesh mesh;
    mesh.pos.reserve(triangleCount * 9u);
    mesh.idx.reserve(triangleCount * 3u);
    mesh.triOwner.reserve(triangleCount);
    mesh.triSurfaceRole.reserve(triangleCount);
    for (std::size_t tri = 0; tri < triangleCount; ++tri) {
        const float angle = static_cast<float>(tri) * 0.0031f;
        const QVector3D a = unit(std::cos(angle), std::sin(angle), 0.2f);
        const QVector3D b = unit(std::cos(angle + 0.001f), std::sin(angle + 0.001f), 0.2f);
        const QVector3D c = tri % 2u == 0u ? a : unit(std::cos(angle), std::sin(angle), 0.201f);
        for (const QVector3D& p : { a, b, c }) {
            mesh.pos.push_back(p.x());
            mesh.pos.push_back(p.y());
            mesh.pos.push_back(p.z());
            mesh.idx.push_back(static_cast<std::uint32_t>(mesh.idx.size()));
        }
        mesh.triOwner.push_back(static_cast<int>(tri % model.cells().size()));
        mesh.triSurfaceRole.push_back(TriangleSurfaceRole::Top);
    }
    return mesh;
}

void testBuilderCompatibilityDeduplicationAndSigns() {
    HexSphereModel model = buildModel(1);
    applyDeterministicCoast(model);
    const TerrainMesh mesh = makeRepeatedMesh(model, 24u);

    SurfaceAtlasBuildOptions bruteOptions;
    bruteOptions.searchMode = ShoreDistanceSearchMode::BruteForce;
    bruteOptions.execution = ShoreDistanceExecution::Serial;
    SurfaceAtlasBuildOptions acceleratedOptions;
    acceleratedOptions.searchMode = ShoreDistanceSearchMode::SpatialIndex;
    acceleratedOptions.execution = ShoreDistanceExecution::Serial;
    const SurfaceAtlasMeshData brute = SurfaceAtlasMeshBuilder::build(mesh, model, bruteOptions);
    const SurfaceAtlasMeshData accelerated = SurfaceAtlasMeshBuilder::build(mesh, model, acceleratedOptions);

    require(accelerated.positions == brute.positions, "accelerated builder changed atlas positions/order");
    require(accelerated.kinds == brute.kinds, "accelerated builder changed surface kinds/order");
    require(accelerated.shoreDistances.size() == brute.shoreDistances.size(), "shore output size changed");
    for (std::size_t i = 0; i < brute.shoreDistances.size(); ++i) {
        require(close(accelerated.shoreDistances[i], brute.shoreDistances[i]),
            "accelerated builder changed signed shore distance");
        if (accelerated.kinds[i] == 3.0f) {
            require(accelerated.shoreDistances[i] >= 0.0f, "sea shore distance must be non-negative");
        } else {
            require(accelerated.shoreDistances[i] <= 0.0f, "land shore distance must be non-positive");
        }
    }
    require(accelerated.stats.cacheHits > 0, "exact duplicate positions were not cached");
}

void testParallelMatchesSerial() {
    HexSphereModel model = buildModel(2);
    applyDeterministicCoast(model);
    const TerrainMesh mesh = makeRepeatedMesh(model, 1800u);
    SurfaceAtlasBuildOptions serialOptions;
    serialOptions.execution = ShoreDistanceExecution::Serial;
    SurfaceAtlasBuildOptions parallelOptions;
    parallelOptions.execution = ShoreDistanceExecution::ParallelAuto;
    const SurfaceAtlasMeshData serial = SurfaceAtlasMeshBuilder::build(mesh, model, serialOptions);
    const SurfaceAtlasMeshData parallel = SurfaceAtlasMeshBuilder::build(mesh, model, parallelOptions);
    require(serial.positions == parallel.positions && serial.kinds == parallel.kinds,
        "parallel builder changed atlas layout");
    require(serial.shoreDistances == parallel.shoreDistances,
        "parallel builder changed deterministic shore distances");
    if (QThreadPool::globalInstance()->maxThreadCount() > 1) {
        require(parallel.stats.workers > 1, "parallel-auto path did not use the shared Qt thread pool");
    }
}

void testEmptyMeshAndNoShoreline() {
    HexSphereModel model = buildModel(1);
    for (Cell& cell : model.cells()) cell.biome = Biome::Grass;
    const SurfaceAtlasMeshData empty = SurfaceAtlasMeshBuilder::build({}, model);
    require(empty.positions.empty() && empty.shoreDistances.empty(), "empty mesh produced atlas vertices");

    const TerrainMesh mesh = makeRepeatedMesh(model, 2u);
    const SurfaceAtlasMeshData noShore = SurfaceAtlasMeshBuilder::build(mesh, model);
    require(noShore.stats.shorelineArcCount == 0, "uniform biome model produced shoreline");
    for (float distance : noShore.shoreDistances) {
        require(close(distance, -kPi * model.waterSurfaceRadius()), "empty shoreline fallback changed");
    }
}

void testL4StructuralReduction() {
    for (int level : { 2, 3, 4 }) {
        HexSphereModel model = buildModel(level);
        auto generator = createTerrainGeneratorByIndex(kDefaultTerrainGeneratorIndex);
        generateCanonicalTerrain(*generator, model, defaultTerrainParams());
        const WaterParams water = resolvedWaterParams(WaterParams{}, &model);
        const CoastalBandData coast = buildCoastalBandData(model, water);
        TerrainMeshOptions meshOptions;
        meshOptions.coastalBand = &coast;
        meshOptions.waterParams = &water;
        const TerrainMesh mesh = TerrainMeshGenerator::buildTerrainMesh(model, meshOptions);
        const SurfaceAtlasMeshData atlas = SurfaceAtlasMeshBuilder::build(mesh, model);
        require(atlas.stats.bruteForceTests > 0, "structural test has no brute-force baseline");
        if (level == 4) {
            require(atlas.stats.exactDistanceTests * 10u < atlas.stats.bruteForceTests,
                "L4 BVH performs at least ten percent of brute-force arc tests");
        }
        require(atlas.stats.atlasVertices == static_cast<std::uint64_t>(atlas.stats.keptTriangles) * 3u,
            "atlas vertex count is inconsistent");
        std::fprintf(stdout,
            "[Perf][SurfaceAtlasTest] level=%d vertices=%llu unique=%llu arcs=%d exact_tests=%llu brute_tests=%llu "
            "index_ms=%.3f query_ms=%.3f workers=%d\n",
            level,
            static_cast<unsigned long long>(atlas.stats.atlasVertices),
            static_cast<unsigned long long>(atlas.stats.uniqueDirections),
            atlas.stats.shorelineArcCount,
            static_cast<unsigned long long>(atlas.stats.exactDistanceTests),
            static_cast<unsigned long long>(atlas.stats.bruteForceTests),
            atlas.stats.indexBuildMs,
            atlas.stats.distanceQueryMs,
            atlas.stats.workers);
    }
}

} // namespace

void runSurfaceAtlasDistanceUnitTests() {
    testSingleArcGeometry();
    testEmptyDegenerateAndExtremeInputs();
    testDeterministicRandomOracle();
    testActualTopologyOracle();
    testBuilderCompatibilityDeduplicationAndSigns();
    testParallelMatchesSerial();
    testEmptyMeshAndNoShoreline();
    testL4StructuralReduction();
}
