#include "tests/SurfaceAtlasDistanceTests.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <stdexcept>

#include <QElapsedTimer>
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

QVector3D loadPosition(const std::vector<float>& data, std::uint32_t index) {
    const std::size_t base = static_cast<std::size_t>(index) * 3u;
    return QVector3D(data[base], data[base + 1u], data[base + 2u]);
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
    // The hot path uses a minimax acos approximation (~1e-3 rad error).
    require(close(bruteForceNearestShoreDistance(unit(0.0f, 0.0f, 1.0f), arcs), kPi * 0.5f, 1.0e-3f),
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
    require(accelerated.indices == brute.indices, "accelerated builder changed atlas index buffer");
    for (std::uint32_t index : accelerated.indices) {
        require(index < accelerated.positions.size() / 3u, "atlas index is out of range");
    }
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

void testAtlasGeometryIsNotQuantized() {
    HexSphereModel model = buildModel(1);
    applyDeterministicCoast(model);

    // Two triangles whose corresponding vertices differ by 0.001. The old
    // coarse distance key (1/250) merged them and distorted the shoreline.
    TerrainMesh mesh;
    auto addTriangle = [&](const QVector3D& offset) {
        const QVector3D points[3] = {
            unit(1.0f, 0.0f, 0.0f) + offset,
            unit(0.0f, 1.0f, 0.0f) + offset,
            unit(0.0f, 0.0f, 1.0f) + offset,
        };
        for (const QVector3D& point : points) {
            mesh.pos.push_back(point.x());
            mesh.pos.push_back(point.y());
            mesh.pos.push_back(point.z());
            mesh.idx.push_back(static_cast<std::uint32_t>(mesh.idx.size()));
        }
        mesh.triOwner.push_back(0);
        mesh.triSurfaceRole.push_back(TriangleSurfaceRole::Top);
    };
    addTriangle(QVector3D(0.0f, 0.0f, 0.0f));
    addTriangle(QVector3D(0.001f, 0.0f, 0.0f));

    const SurfaceAtlasMeshData atlas = SurfaceAtlasMeshBuilder::build(mesh, model);
    require(atlas.positions.size() / 3u == 6u,
        "atlas geometry was coarsely quantized and lost distinct vertices");
    require(atlas.indices.size() == 6u, "atlas index count changed");
    for (std::uint32_t index : atlas.indices) {
        require(index < 6u, "atlas index points to a merged vertex");
    }
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
    require(empty.positions.empty() && empty.shoreDistances.empty() && empty.indices.empty(),
        "empty mesh produced atlas vertices");

    const TerrainMesh mesh = makeRepeatedMesh(model, 2u);
    const SurfaceAtlasMeshData noShore = SurfaceAtlasMeshBuilder::build(mesh, model);
    require(noShore.stats.shorelineArcCount == 0, "uniform biome model produced shoreline");
    for (float distance : noShore.shoreDistances) {
        require(close(distance, -kPi * model.waterSurfaceRadius()), "empty shoreline fallback changed");
    }
}

void testAtlasSourceIsCellCaps() {
    HexSphereModel model = buildModel(2);
    applyDeterministicCoast(model);
    const TerrainMesh source = buildAtlasSourceMesh(model);

    std::size_t expectedTriangles = 0;
    for (const Cell& cell : model.cells()) {
        if (cell.poly.size() >= 3) {
            expectedTriangles += cell.poly.size();
        }
    }
    require(source.idx.size() == expectedTriangles * 3u, "atlas source is not one fan per cell");
    require(source.triOwner.size() == expectedTriangles, "atlas source lost cell owners");

    for (std::size_t tri = 0; tri < source.triOwner.size(); ++tri) {
        const int owner = source.triOwner[tri];
        require(owner >= 0 && static_cast<std::size_t>(owner) < model.cells().size(),
            "atlas source owner is invalid");
        const float expectedRadius = model.radiusForHeight(
            static_cast<float>(model.cells()[static_cast<std::size_t>(owner)].height));
        for (std::size_t corner = 0; corner < 3u; ++corner) {
            const std::uint32_t index = source.idx[tri * 3u + corner];
            const QVector3D position = loadPosition(source.pos, index);
            require(close(position.length(), expectedRadius, 1.0e-5f),
                "atlas source vertex does not use the cell height");
        }
    }
}

void testTerrainTessellationSerialMatchesParallel() {
    for (int level : { 4, 5 }) {
        HexSphereModel model = buildModel(level);
        auto generator = createTerrainGeneratorByIndex(kDefaultTerrainGeneratorIndex);
        generateCanonicalTerrain(*generator, model, defaultTerrainParams());

        TerrainTessellator serial;
        serial.parallel = false;
        serial.smoothMaxDelta = 1;
        serial.outerTrim = 0.15f;
        TerrainTessellator concurrent;
        concurrent.parallel = true;
        concurrent.smoothMaxDelta = 1;
        concurrent.outerTrim = 0.15f;

        const TerrainMesh expected = serial.build(model);
        const TerrainMesh actual = concurrent.build(model);
        require(expected.pos == actual.pos && expected.col == actual.col
                && expected.norm == actual.norm && expected.ore == actual.ore
                && expected.idx == actual.idx && expected.triOwner == actual.triOwner
                && expected.triSurfaceRole == actual.triSurfaceRole
                && expected.cellTriStart == actual.cellTriStart
                && expected.triByCell == actual.triByCell
                && expected.beachTriCount == actual.beachTriCount,
            "parallel tessellation changed deterministic mesh output");
        require(!expected.idx.empty(), "tessellation produced an empty mesh");
        require(expected.cellTriStart.size() == model.cells().size() + 1u,
            "cell triangle CSR has a wrong size");
        require(expected.triByCell.size() == expected.triOwner.size(),
            "cell triangle CSR does not cover all triangles");
        for (std::size_t cellId = 0; cellId < model.cells().size(); ++cellId) {
            for (std::uint32_t slot = expected.cellTriStart[cellId];
                slot < expected.cellTriStart[cellId + 1u]; ++slot) {
                require(expected.triOwner[expected.triByCell[slot]] == static_cast<int>(cellId),
                    "cell triangle CSR points to a foreign triangle");
            }
        }
    }
}

void testCellCapStructuralReduction() {
    for (int level : { 2, 3, 4, 5, 6, 7 }) {
        HexSphereModel model = buildModel(level);
        auto generator = createTerrainGeneratorByIndex(kDefaultTerrainGeneratorIndex);
        generateCanonicalTerrain(*generator, model, defaultTerrainParams());

        QElapsedTimer sourceTimer;
        sourceTimer.start();
        const TerrainMesh source = buildAtlasSourceMesh(model);
        const double sourceMs = sourceTimer.nsecsElapsed() / 1000000.0;

        QElapsedTimer buildTimer;
        buildTimer.start();
        const SurfaceAtlasMeshData atlas = SurfaceAtlasMeshBuilder::build(source, model);
        const double buildMs = buildTimer.nsecsElapsed() / 1000000.0;

        QElapsedTimer tessTimer;
        tessTimer.start();
        TerrainTessellator tessellator;
        tessellator.smoothMaxDelta = 1; // scene default (smoothOneStep)
        tessellator.outerTrim = 0.15f;
        const TerrainMesh renderMesh = tessellator.build(model);
        const double tessMs = tessTimer.nsecsElapsed() / 1000000.0;

        require(atlas.stats.bruteForceTests > 0, "structural test has no brute-force baseline");
        if (level == 4) {
            require(atlas.stats.exactDistanceTests * 10u < atlas.stats.bruteForceTests,
                "L4 BVH performs at least ten percent of brute-force arc tests");
        }
        require(atlas.stats.atlasVertices == static_cast<std::uint64_t>(atlas.stats.keptTriangles) * 3u,
            "atlas vertex count is inconsistent");
        std::fprintf(stdout,
            "[Perf][SurfaceAtlasTest] level=%d cap_tris=%d vertices=%llu unique=%llu exact_dirs=%llu arcs=%d "
            "exact_tests=%llu brute_tests=%llu source_ms=%.3f build_ms=%.3f index_ms=%.3f gate_ms=%.3f "
            "query_ms=%.3f tess_ms=%.3f tess_tris=%llu workers=%d\n",
            level,
            atlas.stats.keptTriangles,
            static_cast<unsigned long long>(atlas.stats.atlasVertices),
            static_cast<unsigned long long>(atlas.stats.uniqueDirections),
            static_cast<unsigned long long>(atlas.stats.exactDirections),
            atlas.stats.shorelineArcCount,
            static_cast<unsigned long long>(atlas.stats.exactDistanceTests),
            static_cast<unsigned long long>(atlas.stats.bruteForceTests),
            sourceMs,
            buildMs,
            atlas.stats.indexBuildMs,
            atlas.stats.shoreGateMs,
            atlas.stats.distanceQueryMs,
            tessMs,
            static_cast<unsigned long long>(renderMesh.idx.size() / 3u),
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
    testAtlasGeometryIsNotQuantized();
    testParallelMatchesSerial();
    testEmptyMeshAndNoShoreline();
    testAtlasSourceIsCellCaps();
    testTerrainTessellationSerialMatchesParallel();
    testCellCapStructuralReduction();
}
