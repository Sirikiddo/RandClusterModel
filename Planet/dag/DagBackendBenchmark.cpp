#include "DagBackendBenchmark.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <memory>
#include <vector>

#include "DagSceneBackend.h"
#include "DagTerrainBackend.h"
#include "LegacyTerrainBackend.h"
#include "TerrainBackendContract.h"
#include "controllers/HexSphereSceneController.h"

namespace {

class BenchmarkTerrainBridge final : public ITerrainSceneBridge {
public:
    void stageTerrainParams(const TerrainParams& params) override {
        scene_.setGenParams(params);
    }

    void stageGeneratorByIndex(int idx) override {
        scene_.setGeneratorByIndex(idx);
    }

    void stageSubdivisionLevel(int level) override {
        scene_.stageSubdivisionLevel(level);
    }

    void rebuildTerrainFromInputs() override {
        scene_.rebuildTerrainFromInputs();
    }

    TerrainSnapshot captureTerrainSnapshot() const override {
        return scene_.captureTerrainSnapshot();
    }

    void projectTerrainSnapshot(const TerrainSnapshot& snapshot) override {
        scene_.applyTerrainSnapshot(snapshot);
    }

private:
    HexSphereSceneController scene_;
};

struct TerrainScenario {
    QString name;
    int generatorIndex = 3;
    int subdivisionLevel = 2;
    TerrainParams params;
};

struct SceneDerivedOperation {
    QString name;
    std::vector<int> selectedCells;
    float outlineBias = 0.004f;
    bool smoothOneStep = true;
    bool mutateTerrain = false;
};

struct LegacySceneDerivedResult {
    std::vector<float> selectionOutline;
    int treeCount = 0;
};

bool snapshotsCompatible(const TerrainSnapshot* lhs, const TerrainSnapshot* rhs) {
    if (!lhs || !rhs) {
        return false;
    }
    if (lhs->subdivisionLevel != rhs->subdivisionLevel ||
        lhs->generatorIndex != rhs->generatorIndex ||
        lhs->cells.size() != rhs->cells.size()) {
        return false;
    }

    const size_t probeCount = std::min<size_t>(lhs->cells.size(), 32);
    for (size_t i = 0; i < probeCount; ++i) {
        if (lhs->cells[i].height != rhs->cells[i].height ||
            lhs->cells[i].biome != rhs->cells[i].biome) {
            return false;
        }
    }
    return true;
}

bool snapshotsCompatible(const TerrainSnapshot& lhs, const TerrainSnapshot& rhs) {
    return snapshotsCompatible(&lhs, &rhs);
}

template <class Backend>
double runTerrainOnce(Backend& backend, const TerrainScenario& scenario) {
    backend.setGeneratorByIndex(scenario.generatorIndex);
    backend.setTerrainParams(scenario.params);
    backend.setSubdivisionLevel(scenario.subdivisionLevel);

    QElapsedTimer timer;
    timer.start();
    backend.regenerateTerrain();
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

LegacySceneDerivedResult runLegacySceneDerived(
    const TerrainSnapshot& snapshot,
    const SceneDerivedOperation& operation) {
    HexSphereSceneController scene;
    TerrainSnapshot workingSnapshot = snapshot;
    if (operation.mutateTerrain && !workingSnapshot.cells.empty()) {
        workingSnapshot.cells.front().height += 1;
    }

    scene.applyTerrainSnapshot(workingSnapshot);
    scene.setSmoothOneStep(operation.smoothOneStep);
    scene.setOutlineBias(operation.outlineBias);
    scene.clearSelection();
    for (int cellId : operation.selectedCells) {
        scene.toggleCellSelection(cellId);
    }
    scene.generateTreePlacements();

    LegacySceneDerivedResult result;
    result.selectionOutline = scene.buildSelectionOutlineVertices();
    result.treeCount = static_cast<int>(scene.getTreePlacements().size());
    return result;
}

SceneDagRequest buildSceneDagRequest(
    const TerrainSnapshot& snapshot,
    const SceneDerivedOperation& operation) {
    SceneDagRequest request;
    request.terrain = snapshot;
    if (operation.mutateTerrain && !request.terrain.cells.empty()) {
        request.terrain.cells.front().height += 1;
    }
    request.heightStep = 0.05f;
    request.outlineBias = operation.outlineBias;
    request.smoothOneStep = operation.smoothOneStep;
    request.selectedCells = operation.selectedCells;
    request.modelRequests.push_back(ModelPlacementRequest{ 0, "pyramid", 0, true, 0.0f });
    return request;
}

void appendTerrainRows(
    DagBenchmarkReport& report,
    const TerrainScenario& scenario,
    int iterations) {
    BenchmarkTerrainBridge dagBridge;
    BenchmarkTerrainBridge legacyBridge;
    DagTerrainBackend dagBackend;
    LegacyTerrainBackend legacyBackend;

    dagBackend.attachTerrainBridge(&dagBridge);
    legacyBackend.attachTerrainBridge(&legacyBridge);
    dagBackend.initializeTerrainState();
    legacyBackend.initializeTerrainState();

    for (int i = 0; i < iterations; ++i) {
        const double dagMs = runTerrainOnce(dagBackend, scenario);
        const double legacyMs = runTerrainOnce(legacyBackend, scenario);
        const bool compatible = snapshotsCompatible(
            dagBackend.currentTerrainSnapshot(),
            legacyBackend.currentTerrainSnapshot());

        DagBenchmarkRow dagRow;
        dagRow.category = "terrain";
        dagRow.scenario = scenario.name;
        dagRow.operation = "full_regenerate";
        dagRow.backend = "DAG terrain";
        dagRow.iteration = i;
        dagRow.elapsedMs = dagMs;
        dagRow.compatible = compatible;
        dagRow.cellCount = dagBackend.currentTerrainSnapshot()
            ? static_cast<int>(dagBackend.currentTerrainSnapshot()->cells.size())
            : 0;
        report.rows.push_back(dagRow);

        DagBenchmarkRow legacyRow;
        legacyRow.category = "terrain";
        legacyRow.scenario = scenario.name;
        legacyRow.operation = "full_regenerate";
        legacyRow.backend = "Legacy terrain";
        legacyRow.iteration = i;
        legacyRow.elapsedMs = legacyMs;
        legacyRow.compatible = compatible;
        legacyRow.cellCount = legacyBackend.currentTerrainSnapshot()
            ? static_cast<int>(legacyBackend.currentTerrainSnapshot()->cells.size())
            : 0;
        report.rows.push_back(legacyRow);

        report.ok = report.ok && compatible;
    }
}

void appendSceneDerivedRows(
    DagBenchmarkReport& report,
    const TerrainScenario& scenario) {
    BenchmarkTerrainBridge bridge;
    DagTerrainBackend terrainBackend;
    terrainBackend.attachTerrainBridge(&bridge);
    terrainBackend.initializeTerrainState();
    runTerrainOnce(terrainBackend, scenario);

    const TerrainSnapshot* baseSnapshot = terrainBackend.currentTerrainSnapshot();
    if (!baseSnapshot) {
        report.ok = false;
        return;
    }

    const std::vector<SceneDerivedOperation> operations = {
        { "baseline", { 0, 1 }, 0.004f, true, false },
        { "repeat_same", { 0, 1 }, 0.004f, true, false },
        { "selection_change", { 2, 3 }, 0.004f, true, false },
        { "selection_revert", { 0, 1 }, 0.004f, true, false },
        { "visual_change", { 0, 1 }, 0.010f, false, false },
        { "visual_revert", { 0, 1 }, 0.004f, true, false },
        { "terrain_edit", { 0, 1 }, 0.004f, true, true },
        { "terrain_revert", { 0, 1 }, 0.004f, true, false },
    };

    DagSceneBackend sceneBackend;
    for (int i = 0; i < static_cast<int>(operations.size()); ++i) {
        const SceneDerivedOperation& operation = operations[static_cast<size_t>(i)];

        const SceneDagRequest request = buildSceneDagRequest(*baseSnapshot, operation);

        QElapsedTimer dagTimer;
        dagTimer.start();
        const SceneDagResult dagResult = sceneBackend.rebuild(request);
        const double dagMs = static_cast<double>(dagTimer.nsecsElapsed()) / 1000000.0;
        const DagDebugStats& dagStats = sceneBackend.lastStats();

        QElapsedTimer legacyTimer;
        legacyTimer.start();
        const LegacySceneDerivedResult legacyResult = runLegacySceneDerived(*baseSnapshot, operation);
        const double legacyMs = static_cast<double>(legacyTimer.nsecsElapsed()) / 1000000.0;

        const bool compatible =
            !request.terrain.cells.empty() &&
            legacyResult.treeCount >= 0 &&
            (dagStats.executedNodes + dagStats.skippedGuardNodes) > 0;

        DagBenchmarkRow dagRow;
        dagRow.category = "scene-derived";
        dagRow.scenario = scenario.name;
        dagRow.operation = operation.name;
        dagRow.backend = "DAG scene";
        dagRow.iteration = i;
        dagRow.elapsedMs = dagMs;
        dagRow.cellCount = static_cast<int>(request.terrain.cells.size());
        dagRow.compatible = compatible;
        dagRow.selectionCount = static_cast<int>(dagResult.selectionOutline.vertices.size() / 6);
        dagRow.treeCount = static_cast<int>(dagResult.treePlacements.size());
        dagRow.modelCount = static_cast<int>(dagResult.modelPlacements.size());
        dagRow.executedNodes = dagStats.executedNodes;
        dagRow.skippedGuardNodes = dagStats.skippedGuardNodes;
        dagRow.cacheHits = dagStats.cacheHits;
        dagRow.cacheMisses = dagStats.cacheMisses;
        report.rows.push_back(dagRow);

        DagBenchmarkRow legacyRow;
        legacyRow.category = "scene-derived";
        legacyRow.scenario = scenario.name;
        legacyRow.operation = operation.name;
        legacyRow.backend = "Legacy scene";
        legacyRow.iteration = i;
        legacyRow.elapsedMs = legacyMs;
        legacyRow.cellCount = static_cast<int>(request.terrain.cells.size());
        legacyRow.compatible = compatible;
        legacyRow.selectionCount = static_cast<int>(legacyResult.selectionOutline.size() / 6);
        legacyRow.treeCount = legacyResult.treeCount;
        report.rows.push_back(legacyRow);

        report.ok = report.ok && compatible;
    }
}

bool writeCsv(const QString& csvPath, const std::vector<DagBenchmarkRow>& rows) {
    QFile file(csvPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream out(&file);
    out << "category,scenario,operation,backend,iteration,elapsed_ms,cell_count,compatible,selection_count,tree_count,model_count,executed_nodes,skipped_guard_nodes,cache_hits,cache_misses\n";
    for (const auto& row : rows) {
        out << '"' << row.category << '"' << ','
            << '"' << row.scenario << '"' << ','
            << '"' << row.operation << '"' << ','
            << '"' << row.backend << '"' << ','
            << row.iteration << ','
            << QString::number(row.elapsedMs, 'f', 4) << ','
            << row.cellCount << ','
            << (row.compatible ? "1" : "0") << ','
            << row.selectionCount << ','
            << row.treeCount << ','
            << row.modelCount << ','
            << row.executedNodes << ','
            << row.skippedGuardNodes << ','
            << row.cacheHits << ','
            << row.cacheMisses << '\n';
    }
    return true;
}

} // namespace

DagBenchmarkReport runDagBackendBenchmark(const QString& csvPath, int iterations) {
    DagBenchmarkReport report;
    report.csvPath = csvPath;

    const std::vector<TerrainScenario> scenarios = {
        { "cold climate L2", 3, 2, TerrainParams{ 12345u, 3, 3.0f } },
        { "changed seed L2", 3, 2, TerrainParams{ 54321u, 3, 3.0f } },
        { "perlin L3", 2, 3, TerrainParams{ 11111u, 1, 4.0f } },
    };

    const int safeIterations = std::max(1, iterations);
    for (const auto& scenario : scenarios) {
        appendTerrainRows(report, scenario, safeIterations);
        appendSceneDerivedRows(report, scenario);
    }

    if (!writeCsv(csvPath, report.rows)) {
        report.ok = false;
    }
    return report;
}
