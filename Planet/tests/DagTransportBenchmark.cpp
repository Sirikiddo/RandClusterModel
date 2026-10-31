#include "DagTransportBenchmark.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <stdexcept>
#include "dag/DagTerrainBackend.h"
#include "dag/DagSceneBackend.h"
#include "dag/DagPathBackend.h"
#include "controllers/PathBuilder.h"
#include "dag/InProcessDagPayloads.h"

namespace {
class SnapshotBridge final : public ITerrainSceneBridge {
public:
    TerrainSnapshot snapshot;
    void stageTerrainParams(const TerrainParams&) override {}
    void stageGeneratorByIndex(int) override {}
    void stageSubdivisionLevel(int) override {}
    void rebuildTerrainFromInputs() override {}
    TerrainSnapshot captureTerrainSnapshot() const override { return snapshot; }
    void projectTerrainSnapshot(const TerrainSnapshot& value) override { snapshot = value; }
};

struct Digest {
    QByteArray bytes;
    QDataStream stream{ &bytes, QIODevice::WriteOnly };
    void number(float v) { stream << (v == 0.0f ? 0.0f : v); }
    void vector(const QVector3D& v) { number(v.x()); number(v.y()); number(v.z()); }
    QString finish() const { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(); }
};

QString digest(const TerrainSnapshot& v) {
    Digest d;
    d.stream << v.subdivisionLevel << v.generatorIndex << v.params.seed << v.params.seaLevel;
    d.number(v.params.scale);
    d.stream << quint64(v.cells.size());
    for (const auto& c : v.cells) {
        d.stream << c.height << int(c.biome) << int(c.oreType);
        d.number(c.temperature); d.number(c.humidity); d.number(c.pressure); d.number(c.oreDensity);
    }
    return d.finish();
}

QString digest(const SceneDagResult& v) {
    Digest d;
    d.stream << quint64(v.selectionOutline.vertices.size());
    for (float x : v.selectionOutline.vertices) d.number(x);
    d.stream << quint64(v.treePlacements.size());
    for (const auto& t : v.treePlacements) {
        d.stream << t.cellId << t.triangleIdx << int(t.placementMode);
        d.number(t.baryU); d.number(t.baryV); d.number(t.baryW);
        d.number(t.scale); d.number(t.rotation);
        d.vector(t.worldPosition); d.vector(t.worldUp); d.number(t.worldYaw); d.number(t.worldScale);
    }
    d.stream << quint64(v.modelPlacements.size());
    for (const auto& m : v.modelPlacements) {
        d.stream << m.entityId << m.meshId << m.cellId << m.selected << m.valid;
        d.vector(m.position); d.vector(m.up);
    }
    return d.finish();
}

QString digest(const PathResult& v) {
    Digest d;
    d.stream << v.found << quint64(v.cellIds.size()); d.number(v.length);
    for (int id : v.cellIds) d.stream << id;
    return d.finish();
}

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void checkPayloadLifetime() {
    InProcessDagPayloads<std::vector<int>> store("test");
    const auto first = store.put(std::vector<int>{1, 2, 3});
    const auto retained = store.resolve(first);
    for (int i = 0; i < 20; ++i) store.put(std::vector<int>{i});
    require(*retained == std::vector<int>({1, 2, 3}), "eviction invalidated a shared payload");
    bool rejected = false;
    try { store.resolve(first); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "expired token was reused");
}

void checkSceneContract() {
    IcosphereBuilder ico;
    HexSphereModel model;
    model.rebuildFromIcosphere(ico.build(2));
    SceneDagRequest request;
    request.terrain.subdivisionLevel = 2;
    request.terrain.topology = model.topology();
    request.terrain.cells.resize(model.cells().size());
    request.selectedCells = {0, 1};
    request.heightStep = 0.05f; request.outlineBias = 0.004f;
    request.modelRequests = {{1, "mesh", 0, false, 0.0f}};
    DagSceneBackend persistent;
    auto check = [&] {
        DagSceneBackend fresh;
        const auto expected = digest(fresh.rebuild(request));
        require(digest(persistent.rebuild(request)) == expected, "scene invalidation differs from fresh calculation");
        require(digest(persistent.rebuild(request)) == expected, "scene repeat differs");
        require(persistent.lastStats().executedNodes == 0, "unchanged scene recomputed");
    };
    check();
    request.heightStep = 0.1f; request.outlineBias = 0.01f; request.smoothOneStep = false; check();
    request.modelRequests[0].surfaceOffset = 0.2f; check();
    request.modelRequests[0].selected = true; request.modelRequests[0].meshId = "changed"; check();
    request.modelRequests[0].cellId = -1; check();
    request.selectedCells = {2, 3}; check();
    request.terrain.cells.back().height = 1; check();
    request.terrain.cells.back().humidity = 0.9f; check();
    request.terrain.params.seed += 1; check();
    request.selectedCells.clear(); request.modelRequests.clear(); check();
}

void checkPathContract() {
    IcosphereBuilder ico;
    HexSphereModel model;
    model.rebuildFromIcosphere(ico.build(2));
    TerrainSnapshot terrain;
    terrain.subdivisionLevel = 2;
    terrain.topology = model.topology();
    terrain.cells.resize(model.cells().size());
    for (auto& cell : model.cells()) { cell.height = 0; cell.biome = Biome::Grass; }
    DagPathBackend backend;
    backend.setTerrainSnapshot(terrain);
    const int goal = model.cells()[0].neighbors.front();
    for (int delta : { 0, 1, 2 }) {
        backend.setSmoothMaxDelta(delta);
        PathBuilder reference(model, delta); reference.build();
        for (const auto [start, end] : { std::pair{0, goal}, std::pair{0, 0}, std::pair{0, 20} }) {
            const auto result = backend.findPath(start, end);
            const auto expected = reference.astar(start, end);
            require(result.cellIds == expected && result.found == !expected.empty(), "path differs from direct A*");
            float length = 0.0f;
            for (size_t i = 1; i < expected.size(); ++i)
                length += PathBuilder::edgeAngularDistance(model.cells()[expected[i - 1]], model.cells()[expected[i]]);
            require(result.length == length, "path length differs");
            require(digest(result) == digest(backend.findPath(start, end)), "repeated path changed");
        }
    }
    const auto expected = digest(backend.findPath(0, goal));
    terrain.cells[goal].height = 100;
    backend.setTerrainSnapshot(terrain);
    require(!backend.findPath(0, goal).found, "stale path after terrain edit");
    terrain.cells[goal].height = 0;
    backend.setTerrainSnapshot(terrain);
    DagPathBackend moved = std::move(backend);
    require(digest(moved.findPath(0, goal)) == expected, "path restore/move failed");
}
}

bool runDagTransportBenchmark(const QString& csvPath) {
    QFile file(csvPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    out << "level,iteration,stage,operation,elapsed_ms,digest\n";
    try {
        checkPayloadLifetime();
        checkPathContract();
        checkSceneContract();
        for (int level : { 2, 4, 6 }) {
            for (int iteration = 0; iteration < 5; ++iteration) {
                auto timed = [&](const char* stage, const char* operation, auto&& action) {
                    QElapsedTimer timer; timer.start();
                    auto result = action();
                    const double ms = timer.nsecsElapsed() / 1e6;
                    out << level << ',' << iteration << ',' << stage << ',' << operation << ','
                        << QString::number(ms, 'f', 6) << ',' << digest(result) << '\n';
                    out.flush();
                    return result;
                };
                SnapshotBridge bridge;
                DagTerrainBackend terrain;
                terrain.attachTerrainBridge(&bridge);
                terrain.setGeneratorByIndex(3);
                terrain.setSubdivisionLevel(level);
                terrain.setTerrainParams(TerrainParams{uint32_t(12345 + iteration), 3, 3.0f});
                auto snapshot = timed("terrain", "generate", [&] {
                    require(bool(terrain.regenerateTerrain()), "terrain generation failed");
                    return *terrain.currentTerrainSnapshot();
                });
                SceneDagRequest request;
                request.terrain = snapshot;
                request.heightStep = 0.05f; request.outlineBias = 0.004f;
                request.selectedCells = {0, 1};
                request.modelRequests = {{1, "pyramid", 0, true, 0.1f}, {2, "invalid", -1, false, 0.0f}};
                DagSceneBackend scene;
                const auto original = timed("scene", "cold", [&] { return scene.rebuild(request); });
                require(digest(original) == digest(timed("scene", "repeat", [&] { return scene.rebuild(request); })), "scene repeat changed");
                request.selectedCells = {2, 3};
                timed("scene", "selection_change", [&] { return scene.rebuild(request); });
                request.selectedCells = {0, 1};
                require(digest(original) == digest(timed("scene", "selection_revert", [&] { return scene.rebuild(request); })), "selection revert changed");
                request.terrain.cells[0].height += 1;
                timed("scene", "terrain_edit", [&] { return scene.rebuild(request); });
                request.terrain = snapshot;
                require(digest(original) == digest(timed("scene", "terrain_revert", [&] { return scene.rebuild(request); })), "terrain revert changed");
                request.modelRequests.clear(); request.selectedCells.clear();
                const auto empty = timed("scene", "empty_requests", [&] { return scene.rebuild(request); });
                require(empty.modelPlacements.empty() && empty.selectionOutline.vertices.empty(), "empty scene requests returned stale data");

                DagPathBackend path;
                timed("path", "cold_with_snapshot", [&] { path.setTerrainSnapshot(snapshot); return path.findPath(0, 1); });
                timed("path", "repeat_with_snapshot", [&] { path.setTerrainSnapshot(snapshot); return path.findPath(0, 1); });
                timed("path", "goal_change", [&] { return path.findPath(0, 2); });

                if (level == 2) {
                    // More distinct requests than the bounded cache can retain.
                    for (int edit = 0; edit < 20; ++edit) {
                        request.terrain.params.seed = 2000 + edit;
                        request.selectedCells = {edit};
                        scene.rebuild(request);
                    }
                    request.terrain = snapshot; request.selectedCells = {0, 1};
                    request.modelRequests = {{1, "pyramid", 0, true, 0.1f}, {2, "invalid", -1, false, 0.0f}};
                    DagSceneBackend moved = std::move(scene);
                    require(digest(moved.rebuild(request)) == digest(original), "scene cache eviction/move changed output");
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        out << "ERROR," << e.what() << '\n';
        return false;
    }
}
