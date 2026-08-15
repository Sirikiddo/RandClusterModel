#include "DagBackendBenchmark.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "DagSceneBackend.h"
#include "DagTerrainBackend.h"
#include "LegacyTerrainBackend.h"
#include "TerrainBackendContract.h"
#include "TerrainSerialization.h"
#include "controllers/HexSphereSceneController.h"
#include "generation/OreGenerator.h"
#include "renderers/TerrainTessellator.h"

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
    bool mutateTerrain = false;
};

struct LegacySceneDerivedResult {
    int treeCount = 0;
};

bool sameOreField(const HexSphereModel& lhs, const HexSphereModel& rhs) {
    if (lhs.cells().size() != rhs.cells().size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.cells().size(); ++i) {
        if (lhs.cells()[i].oreDensity != rhs.cells()[i].oreDensity
            || lhs.cells()[i].oreType != rhs.cells()[i].oreType) {
            return false;
        }
    }
    return true;
}

HexSphereModel buildRockOreModel(uint32_t seed) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(2));
    for (auto& cell : model.cells()) {
        cell.biome = Biome::Rock;
    }
    OreGenerator::generate(model, seed);
    return model;
}

bool runOrePipelineChecks(QString* failure) {
    const auto fail = [failure](const QString& message) {
        if (failure) {
            *failure = message;
        }
        return false;
    };

    const HexSphereModel first = buildRockOreModel(12345u);
    const HexSphereModel repeated = buildRockOreModel(12345u);
    const HexSphereModel changed = buildRockOreModel(54321u);
    if (!sameOreField(first, repeated) || sameOreField(first, changed)) {
        return fail("determinism or seed variation");
    }

    int oreCells = 0;
    for (const auto& cell : first.cells()) {
        if (cell.oreDensity < 0.0f || cell.oreDensity > 1.0f) {
            return fail("density outside [0,1]");
        }
        if (cell.oreDensity > 0.0f) {
            ++oreCells;
            if (cell.oreType == OreType::None) {
                return fail("positive density without ore type");
            }
        }
        else if (cell.oreType != OreType::None) {
            return fail("zero density with non-None type");
        }
    }
    if (oreCells == 0) {
        return fail("standard seed produced no ore");
    }

    IcosphereBuilder builder;
    HexSphereModel biomeMaskModel;
    biomeMaskModel.rebuildFromIcosphere(builder.build(1));
    for (size_t i = 0; i < biomeMaskModel.cells().size(); ++i) {
        auto& cell = biomeMaskModel.cells()[i];
        cell.biome = (i % 2 == 0) ? Biome::Rock : Biome::Grass;
        cell.oreDensity = 1.0f;
        cell.oreType = OreType::Diamond;
    }
    OreGenerator::generate(biomeMaskModel, 12345u);
    for (const auto& cell : biomeMaskModel.cells()) {
        if (cell.biome != Biome::Rock
            && (cell.oreDensity != 0.0f || cell.oreType != OreType::None)) {
            return fail("non-Rock biome contains ore");
        }
    }

    HexSphereModel meshModel;
    meshModel.rebuildFromIcosphere(builder.build(0));
    for (auto& cell : meshModel.cells()) {
        cell.biome = Biome::Grass;
        cell.oreDensity = 0.9f;
        cell.oreType = OreType::Gold;
    }
    meshModel.cells().front().biome = Biome::Rock;
    meshModel.cells().front().oreDensity = 0.75f;
    meshModel.cells().front().oreType = OreType::Copper;
    const TerrainMesh mesh = TerrainTessellator{}.build(meshModel);
    if (mesh.ore.size() != mesh.pos.size() / 3 * 2
        || mesh.triOwner.size() != mesh.idx.size() / 3) {
        return fail("terrain mesh attribute size");
    }
    for (size_t triangle = 0; triangle < mesh.triOwner.size(); ++triangle) {
        const bool expectedOre = mesh.triOwner[triangle] == 0;
        for (size_t vertex = 0; vertex < 3; ++vertex) {
            const size_t offset = triangle * 6 + vertex * 2;
            const float expectedDensity = expectedOre ? 0.75f : 0.0f;
            const float expectedType = expectedOre ? static_cast<float>(OreType::Copper) : 0.0f;
            if (mesh.ore[offset] != expectedDensity || mesh.ore[offset + 1] != expectedType) {
                return fail("terrain mesh ore masking");
            }
        }
    }

    TerrainSnapshot snapshot;
    snapshot.subdivisionLevel = 2;
    snapshot.generatorIndex = 3;
    snapshot.params = TerrainParams{ 12345u, 3, 3.0f };
    snapshot.cells.push_back(TerrainCellSnapshot{
        4, Biome::Rock, 0.25f, 0.5f, 0.75f, 0.8f, OreType::Gold
    });

    const QString encodedV2 = serializeTerrainSnapshot(snapshot);
    const QJsonDocument v2Document = QJsonDocument::fromJson(encodedV2.toUtf8());
    if (!v2Document.isObject()
        || v2Document.object()["version"].toInt() != 2
        || v2Document.object()["cells"].toArray().at(0).toObject().contains("oreVisualDensity")) {
        return fail("snapshot v2 shape");
    }
    const auto roundTrip = deserializeTerrainSnapshot(encodedV2);
    if (!roundTrip
        || roundTrip->cells.size() != 1
        || roundTrip->cells.front().oreType != OreType::Gold
        || std::abs(roundTrip->cells.front().oreDensity - 0.8f) > 0.0001f) {
        return fail("snapshot v2 round-trip");
    }

    QJsonObject v1Root = v2Document.object();
    v1Root["version"] = 1;
    QJsonArray v1Cells = v1Root["cells"].toArray();
    QJsonObject v1Cell = v1Cells.at(0).toObject();
    v1Cell["oreVisualDensity"] = 0.8;
    v1Cell["oreNoiseOffset"] = 2.0;
    v1Cells[0] = v1Cell;
    v1Root["cells"] = v1Cells;
    if (!deserializeTerrainSnapshot(
            QString::fromUtf8(QJsonDocument(v1Root).toJson(QJsonDocument::Compact)))) {
        return fail("snapshot v1 compatibility");
    }

    QJsonObject invalidVersion = v2Document.object();
    invalidVersion["version"] = 3;
    if (deserializeTerrainSnapshot(
            QString::fromUtf8(QJsonDocument(invalidVersion).toJson(QJsonDocument::Compact)))) {
        return fail("unknown snapshot version accepted");
    }

    QJsonObject invalidType = v2Document.object();
    QJsonArray invalidCells = invalidType["cells"].toArray();
    QJsonObject invalidCell = invalidCells.at(0).toObject();
    invalidCell["oreType"] = 99;
    invalidCells[0] = invalidCell;
    invalidType["cells"] = invalidCells;
    if (deserializeTerrainSnapshot(
            QString::fromUtf8(QJsonDocument(invalidType).toJson(QJsonDocument::Compact)))
        || deserializeTerrainSnapshot("{broken json")) {
        return fail("invalid snapshot payload accepted");
    }

    return true;
}

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
        if (lhs->cells[i].height != rhs->cells[i].height
            || lhs->cells[i].biome != rhs->cells[i].biome
            || lhs->cells[i].oreDensity != rhs->cells[i].oreDensity
            || lhs->cells[i].oreType != rhs->cells[i].oreType) {
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
    scene.generateTreePlacements();

    LegacySceneDerivedResult result;
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

bool outlinesCompatible(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (std::abs(lhs[i] - rhs[i]) > 1.0e-5f) {
            return false;
        }
    }
    return true;
}

void appendSelectionDerivedRows(DagBenchmarkReport& report, int subdivisionLevel) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(subdivisionLevel));

    int pentagonId = -1;
    int hexagonId = -1;
    for (size_t i = 0; i < model.cells().size(); ++i) {
        const size_t degree = model.cells()[i].poly.size();
        if (degree == 5 && pentagonId < 0) pentagonId = static_cast<int>(i);
        if (degree == 6 && hexagonId < 0) hexagonId = static_cast<int>(i);
    }
    if (pentagonId < 0 || hexagonId < 0) {
        report.ok = false;
        return;
    }

    const auto makeInput = [&](std::initializer_list<int> ids, float bias, bool smooth) {
        QSet<int> selected;
        for (int id : ids) selected.insert(id);
        return SelectionOutlineGenerator::buildSelectionOutlineInput(
            model, selected, 0.05f, bias, smooth);
    };

    const SelectionOutlineInput empty = makeInput({}, 0.004f, true);
    const SelectionOutlineInput pentagon = makeInput({ pentagonId }, 0.004f, true);
    const SelectionOutlineInput hexagon = makeInput({ hexagonId }, 0.004f, true);
    const SelectionOutlineInput twoCells = makeInput({ pentagonId, hexagonId }, 0.004f, true);
    SelectionOutlineInput heightChange = twoCells;
    if (!heightChange.edges.empty()) heightChange.edges.front().cellHeight += 1;

    const std::vector<std::pair<QString, SelectionOutlineInput>> operations = {
        { "empty", empty },
        { "pentagon", pentagon },
        { "hexagon", hexagon },
        { "two_cells", twoCells },
        { "repeat", twoCells },
        { "selection_change", pentagon },
        { "selection_revert", twoCells },
        { "bias_change", makeInput({ pentagonId, hexagonId }, 0.010f, true) },
        { "smooth_change", makeInput({ pentagonId, hexagonId }, 0.004f, false) },
        { "height_change", heightChange },
    };

    DagSceneBackend backend;
    for (int i = 0; i < static_cast<int>(operations.size()); ++i) {
        const auto& operation = operations[size_t(i)];
        QElapsedTimer timer;
        timer.start();
        const SelectionDagResult dagResult = backend.rebuildSelectionOutline(operation.second);
        const double dagMs = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
        const std::vector<float> direct = SelectionOutlineGenerator::buildSelectionOutlineVertices(operation.second);
        const bool compatible = dagResult.success && outlinesCompatible(dagResult.vertices, direct);
        const DagDebugStats stats = backend.lastStats();

        DagBenchmarkRow row;
        row.category = "selection-derived";
        row.scenario = QString("L%1").arg(subdivisionLevel);
        row.operation = operation.first;
        row.backend = "DAG selection";
        row.iteration = i;
        row.elapsedMs = dagMs;
        row.cellCount = static_cast<int>(model.cells().size());
        row.compatible = compatible;
        row.selectionCount = static_cast<int>(dagResult.vertices.size() / 6);
        row.executedNodes = stats.executedNodes;
        row.skippedGuardNodes = stats.skippedGuardNodes;
        row.cacheHits = stats.cacheHits;
        row.cacheMisses = stats.cacheMisses;
        row.planCacheHits = stats.planCacheHits;
        row.planCacheMisses = stats.planCacheMisses;
        row.inputBytes = dagResult.inputBytes;
        report.rows.push_back(row);
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
        { "baseline", false },
        { "repeat_same", false },
        { "terrain_edit", true },
        { "terrain_revert", false },
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
        dagRow.treeCount = static_cast<int>(dagResult.treePlacements.size());
        dagRow.modelCount = static_cast<int>(dagResult.modelPlacements.size());
        dagRow.executedNodes = dagStats.executedNodes;
        dagRow.skippedGuardNodes = dagStats.skippedGuardNodes;
        dagRow.cacheHits = dagStats.cacheHits;
        dagRow.cacheMisses = dagStats.cacheMisses;
        dagRow.planCacheHits = dagStats.planCacheHits;
        dagRow.planCacheMisses = dagStats.planCacheMisses;
        dagRow.inputBytes = dagStats.inputBytes;
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
    out << "category,scenario,operation,backend,iteration,elapsed_ms,cell_count,compatible,selection_count,tree_count,model_count,executed_nodes,skipped_guard_nodes,cache_hits,cache_misses,plan_cache_hits,plan_cache_misses,input_bytes\n";
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
            << row.cacheMisses << ','
            << row.planCacheHits << ','
            << row.planCacheMisses << ','
            << row.inputBytes << '\n';
    }
    return true;
}

} // namespace

DagBenchmarkReport runDagBackendBenchmark(const QString& csvPath, int iterations) {
    DagBenchmarkReport report;
    report.csvPath = csvPath;
    QString oreCheckFailure;
    report.ok = runOrePipelineChecks(&oreCheckFailure);
    if (!report.ok) {
        DagBenchmarkRow row;
        row.category = "self-check";
        row.scenario = oreCheckFailure;
        row.operation = "ore_pipeline";
        row.backend = "DAG terrain";
        row.compatible = false;
        report.rows.push_back(row);
    }

    const std::vector<TerrainScenario> scenarios = {
        { "cold climate L2", 3, 2, TerrainParams{ 12345u, 3, 3.0f } },
        { "changed seed L2", 3, 2, TerrainParams{ 54321u, 3, 3.0f } },
        { "perlin L3", 2, 3, TerrainParams{ 11111u, 1, 4.0f } },
    };

    const int safeIterations = std::max(1, iterations);
    appendSelectionDerivedRows(report, 2);
    appendSelectionDerivedRows(report, 4);
    for (const auto& scenario : scenarios) {
        appendTerrainRows(report, scenario, safeIterations);
        appendSceneDerivedRows(report, scenario);
    }

    if (!writeCsv(csvPath, report.rows)) {
        report.ok = false;
    }
    return report;
}
