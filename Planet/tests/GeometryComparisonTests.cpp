#include "tests/GeometryComparisonTests.h"
#include "model/IcosahedralGrid.h"
#include "model/HexSphereTopology.h"
#include "controllers/HexSphereSceneController.h"
#include "dag/DagTerrainBackend.h"
#include "dag/DagSceneBackend.h"
#include "dag/DagPathBackend.h"
#include "dag/InProcessDagPayloads.h"
#include "dag/TerrainSerialization.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void compareMeshes(const TerrainMesh& a, const TerrainMesh& b) {
    require(a.pos == b.pos && a.col == b.col && a.norm == b.norm && a.ore == b.ore,
        "Direct writer changed vertex attributes");
    require(a.idx == b.idx && a.triOwner == b.triOwner && a.triSurfaceRole == b.triSurfaceRole,
        "Direct writer changed triangle indices/ownership/roles");
    require(a.cellTriStart == b.cellTriStart && a.triByCell == b.triByCell && a.beachTriCount == b.beachTriCount,
        "Direct writer changed river index or beach counts");
}

void checkTopology() {
    for (int level : {0, 1, 2, 3}) {
        const int n = 1 << level;
        const IcosahedralGrid grid(n);
        const auto topology = buildHexSphereTopology(level, SphereGeometry::GlobalVoronoi);
        require(topology != buildHexSphereTopology(level), "Grid cache mixed geometry kinds");
        require(topology == buildHexSphereTopology(level, SphereGeometry::GlobalVoronoi), "Grid cache missed");
        require(topology->cells.size() == grid.cellCount() && topology->pentagonCount == 12, "Wrong grid counts");
        require(topology->dualVerts.size() == 20u*n*n && topology->wireEdges.size() == 30u*n*n, "Wrong dual counts");
        for (const auto& cell : topology->cells) {
            const auto address = grid.address(cell.id);
            require(grid.id(address) == static_cast<unsigned>(cell.id), "Dense ID/address mismatch");
            require(grid.id(grid.locate({cell.centroid.x(), cell.centroid.y(), cell.centroid.z()})) == static_cast<unsigned>(cell.id),
                "Cell center did not locate to itself");
            require(cell.poly.size() == cell.neighbors.size() && (cell.poly.size() == 5 || cell.poly.size() == 6), "Wrong cell degree");
            for (size_t k = 0; k < cell.poly.size(); ++k) {
                const int a = cell.poly[k], b = cell.poly[(k+1)%cell.poly.size()];
                const auto& other = topology->cells.at(cell.neighbors[k]);
                bool reverse = false;
                for (size_t j = 0; j < other.poly.size(); ++j)
                    if (other.neighbors[j] == cell.id && other.poly[j] == b && other.poly[(j+1)%other.poly.size()] == a) reverse = true;
                require(reverse, "Shared edge has inconsistent winding/neighbors/vertex IDs");
                const auto& p = topology->dualVerts[a];
                const auto& q = topology->dualVerts[b];
                require(QVector3D::dotProduct(QVector3D::crossProduct(p-cell.centroid,q-cell.centroid),cell.centroid) > 0,
                    "Cell fan winding is inverted");
                require(std::abs(QVector3D::dotProduct(p,cell.centroid-other.centroid)) < 2e-6f,
                    "Cell edge does not lie on its neighbors' bisector");
            }
        }
    }
}

void checkWriters() {
    for (SphereGeometry geometry : {SphereGeometry::RecursiveCentroid, SphereGeometry::GlobalVoronoi}) {
        for (int variant = 0; variant < 5; ++variant) {
            const int level = variant == 4 ? 5 : 2;
            HexSphereModel model(buildHexSphereTopology(level, geometry));
            model.setHeightStep(0.02f);
            for (auto& cell : model.cells()) {
                cell.height = cell.id % 7 - 3;
                cell.biome = cell.height < 0 ? Biome::Sea : Biome::Rock;
                cell.oreDensity = 0.4f;
                cell.oreType = OreType::Iron;
            }
            const WaterParams water = waterParamsForPreset(WaterPreset::Temperate);
            const auto coast = buildCoastalBandData(model, water);
            TerrainTessellator writer;
            writer.parallel = variant == 4;
            writer.smoothMaxDelta = variant % 2;
            writer.doCaps = variant != 1;
            writer.doBlades = variant != 2;
            writer.doCornerTris = variant != 3;
            writer.doEdgeCliffs = variant != 2;
            writer.coastalBand = &coast;
            writer.waterParams = &water;
            const auto reference = writer.build(model);
            writer.directWrite = true;
            const auto direct = writer.build(model);
            compareMeshes(reference, direct);
            for (size_t c = 0; c < model.cells().size(); ++c)
                for (size_t j = direct.cellTriStart[c]; j < direct.cellTriStart[c+1]; ++j)
                    require(direct.triOwner[direct.triByCell[j]] == static_cast<int>(c), "River CSR has wrong owner");
        }
    }
}

class Bridge final : public ITerrainSceneBridge {
public:
    HexSphereSceneController scene;
    void stageTerrainParams(const TerrainParams& p) override { scene.setGenParams(p); }
    void stageGeneratorByIndex(int i) override { scene.setGeneratorByIndex(i); }
    void stageSubdivisionLevel(int l) override { scene.stageSubdivisionLevel(l); }
    void stageGeometry(SphereGeometry g) override { scene.stageGeometry(g); }
    void rebuildTerrainFromInputs() override { scene.rebuildTerrainFromInputs(); }
    TerrainSnapshot captureTerrainSnapshot() const override { return scene.captureTerrainSnapshot(); }
    void projectTerrainSnapshot(const TerrainSnapshot& s) override { scene.applyTerrainSnapshot(s); }
};

void checkSwitchingAndSnapshots() {
    Bridge bridge;
    DagTerrainBackend backend;
    backend.attachTerrainBridge(&bridge);
    backend.initializeTerrainState();
    DagSceneBackend sceneDag;
    DagPathBackend pathDag;
    TerrainSnapshot original;
    for (int step = 0; step < 4; ++step) {
        const auto geometry = step == 1 || step == 2 ? SphereGeometry::GlobalVoronoi : SphereGeometry::RecursiveCentroid;
        bridge.scene.setDirectMeshWrite(step >= 2);
        backend.setGeometry(geometry);
        require(bool(backend.regenerateTerrain()), "DAG grid switching failed");
        const auto stats = backend.lastBuildStats();
        require(stats.geometryExecutions == (step == 2 ? 0 : 1)
            && stats.terrainExecutions == 1 && stats.oreExecutions == 1,
            "Geometry/Terrain/Ore DAG dependencies executed the wrong nodes");
        auto snapshot = bridge.scene.captureTerrainSnapshot();
        require(snapshot.geometry == geometry && snapshot.topology->geometry == geometry, "Geometry lost in DAG/scene");
        if (step == 0) original = snapshot;
        if (step == 3) require(sameTerrainSnapshot(original, snapshot), "Returning to original changed deterministic terrain");
        const auto restored = deserializeTerrainSnapshot(serializeTerrainSnapshot(snapshot));
        require(restored && restored->geometry == geometry && sameTerrainSnapshot(snapshot, *restored), "Snapshot round trip lost geometry/state");
        bridge.scene.applyTerrainSnapshot(*restored);
        require(bridge.scene.model().geometry() == geometry, "Restored snapshot attached wrong geometry");

        // Identical fields on different grids must still invalidate the DAG caches.
        for (auto& cell : snapshot.cells) { cell.height = 0; cell.biome = Biome::Grass; }
        snapshot.treeCells.clear(); snapshot.riverCells.clear();
        SceneDagRequest request;
        request.terrain = snapshot;
        request.heightStep = bridge.scene.heightStep();
        request.selectedCells = {20};
        const auto result = sceneDag.rebuild(request);
        require(!result.selectionOutline.vertices.empty(), "Selection outline lost after grid switch");
        for (const auto& tree : result.treePlacements) {
            require(tree.cellId >= 0 && tree.cellId < snapshot.topology->cellCount(), "Invalid tree cell");
            require(tree.triangleIdx >= 0 && tree.triangleIdx < static_cast<int>(snapshot.topology->cells[tree.cellId].poly.size()), "Invalid tree fan index");
        }
        pathDag.setTerrainSnapshot(snapshot);
        const int neighbor = snapshot.topology->cells[20].neighbors[0];
        const auto path = pathDag.findPath(20, neighbor);
        require(path.found && path.cellIds.front() == 20 && path.cellIds.back() == neighbor, "Path failed on new adjacency");
        snapshot.treeCells = {neighbor};
        pathDag.setTerrainSnapshot(snapshot);
        require(!pathDag.findPath(20, neighbor).found, "Path cache ignored changed occupancy");
    }
    const auto baseline = *backend.currentTerrainSnapshot();
    auto changedParams = baseline.params;
    for (int edit = 0; edit < 3; ++edit) {
        if (edit == 0) ++changedParams.seed;
        if (edit == 1) ++changedParams.seaLevel;
        if (edit == 2) changedParams.scale += 0.5f;
        backend.setTerrainParams(changedParams);
        require(bool(backend.regenerateTerrain()), "Terrain parameter regeneration failed");
        const auto stats = backend.lastBuildStats();
        require(stats.geometryExecutions == 0 && stats.terrainExecutions == 1 && stats.oreExecutions == 1,
            "Changing terrain parameters rebuilt geometry or skipped terrain/ore");
        require(backend.currentTerrainSnapshot()->topology == baseline.topology, "Seed/sea/scale changed topology handle");
    }
    backend.setTerrainParams(baseline.params);
    require(bool(backend.regenerateTerrain()) && sameTerrainSnapshot(baseline, *backend.currentTerrainSnapshot()),
        "Persistent DAG retained stale terrain/ore state after parameter revert");
    backend.setGeometry(SphereGeometry::GlobalVoronoi);
    backend.setSubdivisionLevel(3);
    require(bool(backend.regenerateTerrain()) && backend.lastBuildStats().geometryExecutions == 1,
        "Level change failed to execute GeometryBuild");
    const auto lastValid = *backend.currentTerrainSnapshot();
    backend.setSubdivisionLevel(8); // Deliberate materialized-grid failure.
    require(!backend.regenerateTerrain(), "Unsupported geometry level unexpectedly succeeded");
    require(sameTerrainSnapshot(lastValid, *backend.currentTerrainSnapshot()), "Failed DAG run published partial state");
    backend.setSubdivisionLevel(2);
    backend.setGeometry(SphereGeometry::RecursiveCentroid);
    require(bool(backend.regenerateTerrain()) && sameTerrainSnapshot(baseline, *backend.currentTerrainSnapshot()),
        "DAG could not recover after geometry failure");
    auto alternate = original;
    alternate.geometry = SphereGeometry::GlobalVoronoi;
    require(!sameTerrainSnapshot(original, alternate), "Snapshot identity ignores grid kind");
    auto json = QJsonDocument::fromJson(serializeTerrainSnapshot(original).toUtf8()).object();
    json["version"] = 3; json.remove("geometry");
    const auto old = deserializeTerrainSnapshot(QString::fromUtf8(QJsonDocument(json).toJson()));
    require(old && old->geometry == SphereGeometry::RecursiveCentroid, "Legacy snapshot did not retain legacy grid");
    json["version"] = 4; json["geometry"] = 99;
    require(!deserializeTerrainSnapshot(QString::fromUtf8(QJsonDocument(json).toJson())), "Unknown grid accepted");
}
}

void runGeometryComparisonTests() {
    checkTopology();
    checkWriters();
    checkSwitchingAndSnapshots();
    qInfo() << "Geometry comparison tests passed: seams, writers, snapshots, switching, scene and path DAG";
}
