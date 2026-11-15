#include "DagTerrainBackend.h"

#include <QElapsedTimer>
#include <QtDebug>

#include <optional>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

#include "generation/TerrainGenerator.h"
#include "generation/OreGenerator.h"
#include "model/HexSphereModel.h"

#include <proc/ProcessDag.h>
#include <proc/Schema.h>
#include <proc/Logging.h>
#include <proc/FileSchema.h>
#include <proc/ScenarioReader.h>

namespace {
// Temporary adapter handoff for one process and one downstream consumer per
// snapshot. The DAG carries only a token; the consumer receives the immutable
// structure through shared_ptr. This side channel is not a general transport
// contract: replace it with typed DAG payloads before supporting other processes
// or independent consumers. Never encode a raw pointer in the token.
// The persistent engine retains the latest topology. Snapshot slots are cleared
// before each regeneration; revision tokens prevent stale outputs from matching.
struct TerrainSnapshotHandoff {
    using Snapshot = std::shared_ptr<const TerrainSnapshot>;
    std::string topologyToken;
    std::string baseToken;
    std::string finalToken;
    std::uint64_t revision = 0;
    HexSphereTopologyPtr topology;
    TerrainBuildStats stats;

    Snapshot base;
    Snapshot final;

    Snapshot resolve(std::string_view token) const {
        if (token == baseToken) return base;
        if (token == finalToken) return final;
        return {};
    }
};

TerrainSnapshot buildBaseTerrainSnapshot(int generatorIndex, HexSphereTopologyPtr topology, const TerrainParams& params, HexSphereModel& model) {
    QElapsedTimer timer;
    timer.start();
    // Mutable generator scratch space is private to this node. Its immutable
    // topology is supplied by GeometryBuild, never built as a side effect here.
    if (model.topology() != topology) {
        model.setTopology(topology);
    }
    else {
        model.resetCellStates();
    }
    const double stateSetupMs = timer.nsecsElapsed() / 1000000.0;

    timer.restart();
    auto generator = createTerrainGeneratorByIndex(generatorIndex);
    generateCanonicalTerrain(*generator, model, params);
    const double terrainMs = timer.nsecsElapsed() / 1000000.0;

    timer.restart();
    TerrainSnapshot snapshot;
    snapshot.geometry = topology->geometry;
    snapshot.generatorIndex = normalizeTerrainGeneratorIndex(generatorIndex);
    snapshot.subdivisionLevel = topology->subdivisionLevel;
    snapshot.params = params;
    snapshot.topology = model.topology();
    snapshot.cells.reserve(model.cells().size());

    for (const auto& cell : model.cells()) {
        TerrainCellSnapshot cellSnapshot;
        cellSnapshot.height = cell.height;
        cellSnapshot.biome = cell.biome;
        cellSnapshot.temperature = cell.temperature;
        cellSnapshot.humidity = cell.humidity;
        cellSnapshot.pressure = cell.pressure;
        cellSnapshot.oreDensity = cell.oreDensity;
        cellSnapshot.oreType = cell.oreType;
        snapshot.cells.push_back(cellSnapshot);
    }
    qInfo().nospace() << "[Perf][Generation] stage=dag_terrain_build"
        << " level=" << snapshot.subdivisionLevel
        << " cells=" << model.cellCount()
        << " state_setup_ms=" << stateSetupMs
        << " terrain_ms=" << terrainMs
        << " snapshot_ms=" << timer.nsecsElapsed() / 1000000.0;
    return snapshot;
}

TerrainSnapshot generateOreField(TerrainSnapshot snapshot) {
    if (!snapshot.topology || snapshot.cells.size() != snapshot.topology->cells.size())
        throw std::invalid_argument("OreBuild requires matching topology and cell state");
    // OreBuild consumes only its declared snapshot, not TerrainBuild's mutable
    // model. The topology overload avoids copying adjacency into another model.
    std::vector<TerrainCellState> state(snapshot.cells.size());
    for (size_t i = 0; i < state.size(); ++i) state[i].biome = snapshot.cells[i].biome;
    if (normalizeTerrainGeneratorIndex(snapshot.generatorIndex) == 0) {
        OreGenerator::clear(state);
    } else {
        OreGenerator::generate(*snapshot.topology, state, snapshot.params.seed);
    }

    const size_t count = state.size();
    for (size_t i = 0; i < count; ++i) {
        snapshot.cells[i].oreDensity = state[i].oreDensity;
        snapshot.cells[i].oreType = state[i].oreType;
    }
    return snapshot;
}

} // namespace

struct DagTerrainBackend::Impl {
    Impl() {
        schema = std::make_unique<proc::GraphSchema>(buildSchema());
        guardRegistry = std::make_unique<proc::GuardRegistry>(proc::make_builtin_guard_registry());
        outputs = std::make_unique<std::vector<proc::Field>>(std::initializer_list<proc::Field>{ "terrainSnapshot" });
    }

    ITerrainSceneBridge* bridge = nullptr;
    TerrainParams params = defaultTerrainParams();
    int generatorIndex = kDefaultTerrainGeneratorIndex;
    int subdivisionLevel = kDefaultTerrainSubdivisionLevel;
    SphereGeometry geometry = SphereGeometry::RecursiveCentroid;
    TerrainSnapshotHandoff::Snapshot currentSnapshot;
    // Reused across DAG runs so a seed change does not copy the whole topology.
    std::shared_ptr<HexSphereModel> model = std::make_shared<HexSphereModel>();

    std::unique_ptr<proc::GraphSchema> schema;
    std::unique_ptr<proc::GuardRegistry> guardRegistry;
    std::unique_ptr<std::vector<proc::Field>> outputs;
    std::shared_ptr<TerrainSnapshotHandoff> handoff = std::make_shared<TerrainSnapshotHandoff>();
    std::unique_ptr<proc::DefaultDagEngine> engine;

    static proc::GraphSchema buildSchema() {
        proc::GraphSchema::StorageLayout roles;
        roles.inputs.insert("generatorIndex");
        roles.inputs.insert("seed");
        roles.inputs.insert("seaLevel");
        roles.inputs.insert("scale");
        roles.inputs.insert("subdivisionLevel");
        roles.inputs.insert("geometry");
        roles.inputs.insert("geometryDirty");
        roles.inputs.insert("regeneration");
        roles.outputs.insert("terrainSnapshot");

        return proc::GraphSchemaBuilder::compile(
            roles,
            {
                {"generatorIndex", "int"},
                {"seed", "int"},
                {"seaLevel", "int"},
                {"scale", "scalar"},
                {"subdivisionLevel", "int"},
                {"geometry", "int"},
                {"geometryDirty", "int"},
                {"regeneration", "str"},
                {"topology", "str"},
                {"baseTerrainSnapshot", "str"},
                {"terrainSnapshot", "str"},
            },
            {
                proc::GraphSchemaBuilder::NodeDef{
                    "GeometryBuild", "buildGeometry",
                    {"subdivisionLevel", "geometry", "geometryDirty"},
                    {"topology"},
                    proc::GraphSchemaBuilder::GuardDef{ "geometryDirty", "1" },
                },
                proc::GraphSchemaBuilder::NodeDef{
                    "TerrainBuild",
                    "buildTerrain",
                    {"topology", "generatorIndex", "seed", "seaLevel", "scale", "regeneration"},
                    {"baseTerrainSnapshot"},
                    std::nullopt,
                },
                proc::GraphSchemaBuilder::NodeDef{
                    "OreBuild",
                    "generateOre",
                    {"baseTerrainSnapshot"},
                    {"terrainSnapshot"},
                    std::nullopt,
                },
            },
            makeOperationRegistry(),
            proc::make_builtin_algebra_registry());
    }

    static proc::OperationRegistry makeOperationRegistry() {
        proc::OperationRegistry registry = proc::make_builtin_operation_registry();
        registry.register_op("generateOre", proc::v2::OpId{ 300 });
        registry.register_op("buildGeometry", proc::v2::OpId{ 301 });
        return registry;
    }

    static int readIntField(
        const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
        const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
        proc::v2::FieldSlot slot,
        int fallback) {
        const auto handle = readHandle(slot);
        const auto debugView = proc::Commit::debug_view(handle);
        const auto debugValue = QString::fromUtf8(debugView.data(), static_cast<qsizetype>(debugView.size()));
        bool ok = false;
        const int parsed = debugValue.toInt(&ok);
        if (!ok) {
            qWarning() << "DagTerrainBackend failed to parse DAG int field" << fieldName(slot).data() << "from" << debugValue;
            return fallback;
        }
        return parsed;
    }

    static float readFloatField(
        const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
        const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
        proc::v2::FieldSlot slot,
        float fallback) {
        const auto handle = readHandle(slot);
        const auto debugView = proc::Commit::debug_view(handle);
        const auto debugValue = QString::fromUtf8(debugView.data(), static_cast<qsizetype>(debugView.size()));
        bool ok = false;
        const float parsed = debugValue.toFloat(&ok);
        if (!ok) {
            qWarning() << "DagTerrainBackend failed to parse DAG scalar field" << fieldName(slot).data() << "from" << debugValue;
            return fallback;
        }
        return parsed;
    }

    static proc::RuntimeOperationRegistry buildRuntimeRegistry(
        const proc::GraphSchema& schema,
        const std::shared_ptr<TerrainSnapshotHandoff>& handoff,
        const std::shared_ptr<HexSphereModel>& model) {
        proc::RuntimeOperationRegistry registry(makeOperationRegistry());
        const auto nodeSlot = schema.find_node("TerrainBuild");
        const auto baseOutputSlot = schema.find_field("baseTerrainSnapshot");
        const auto oreNodeSlot = schema.find_node("OreBuild");
        const auto outputSlot = schema.find_field("terrainSnapshot");
        const auto generatorSlot = schema.find_field("generatorIndex");
        const auto seedSlot = schema.find_field("seed");
        const auto seaLevelSlot = schema.find_field("seaLevel");
        const auto scaleSlot = schema.find_field("scale");
        const auto subdivisionSlot = schema.find_field("subdivisionLevel");
        const auto geometrySlot = schema.find_field("geometry");
        const auto geometryNodeSlot = schema.find_node("GeometryBuild");
        const auto topologySlot = schema.find_field("topology");

        if (!nodeSlot || !baseOutputSlot || !oreNodeSlot || !outputSlot || !generatorSlot || !seedSlot || !seaLevelSlot || !scaleSlot || !subdivisionSlot || !geometrySlot || !geometryNodeSlot || !topologySlot) {
            throw std::runtime_error("DagTerrainBackend failed to bind terrain DAG schema slots");
        }

        registry.bind_executor(schema.op_of(*geometryNodeSlot), *geometryNodeSlot,
            [schema, handoff, outputSlot = *topologySlot, subdivisionSlot = *subdivisionSlot, geometrySlot = *geometrySlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                QElapsedTimer timer;
                timer.start();
                const int level = readIntField(readHandle, fieldName, subdivisionSlot, kDefaultTerrainSubdivisionLevel);
                const auto geometry = static_cast<SphereGeometry>(readIntField(readHandle, fieldName, geometrySlot, 0));
                handoff->topology = buildHexSphereTopology(level, geometry);
                handoff->topologyToken = "topology:" + std::to_string(handoff->revision);
                ++handoff->stats.geometryExecutions;
                qInfo().nospace() << "[Perf][Generation] stage=dag_geometry_build level=" << level
                    << " geometry=" << static_cast<int>(geometry) << " ms=" << timer.nsecsElapsed() / 1000000.0;
                proc::Commit commit;
                commit.set(outputSlot, handoff->topologyToken, proc::v2::WriteLifetime::Persistent,
                    std::string(schema.field_name(outputSlot)));
                return commit;
            });
        registry.bind_executor(
            schema.op_of(*nodeSlot),
            *nodeSlot,
            [schema, handoff, model, outputSlot = *baseOutputSlot, generatorSlot = *generatorSlot, seedSlot = *seedSlot,
             seaLevelSlot = *seaLevelSlot, scaleSlot = *scaleSlot, topologySlot = *topologySlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                const TerrainParams defaults = defaultTerrainParams();
                TerrainParams params;
                params.seed = static_cast<uint32_t>(Impl::readIntField(
                    readHandle, fieldName, seedSlot, static_cast<int>(defaults.seed)));
                params.seaLevel = Impl::readIntField(readHandle, fieldName, seaLevelSlot, defaults.seaLevel);
                params.scale = Impl::readFloatField(readHandle, fieldName, scaleSlot, defaults.scale);

                const int generatorIndex = Impl::readIntField(
                    readHandle, fieldName, generatorSlot, kDefaultTerrainGeneratorIndex);
                if (!handoff->topology || proc::Commit::debug_view(readHandle(topologySlot)) != handoff->topologyToken)
                    throw std::runtime_error("TerrainBuild requires GeometryBuild output");

                handoff->base = std::make_shared<const TerrainSnapshot>(
                    buildBaseTerrainSnapshot(generatorIndex, handoff->topology, params, *model));
                handoff->baseToken = "terrain:base:" + std::to_string(handoff->revision);
                ++handoff->stats.terrainExecutions;

                proc::Commit commit;
                commit.set(
                    outputSlot,
                    handoff->baseToken,
                    proc::v2::WriteLifetime::Persistent,
                    std::string(schema.field_name(outputSlot)));
                return commit;
            });
        registry.bind_executor(
            schema.op_of(*oreNodeSlot),
            *oreNodeSlot,
            [schema, handoff, baseSlot = *baseOutputSlot, outputSlot = *outputSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn&,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                proc::Commit commit;
                const auto handle = readHandle(baseSlot);
                const auto snapshot = handoff->resolve(proc::Commit::debug_view(handle));
                if (!snapshot) {
                    throw std::runtime_error("Missing terrain snapshot in this DAG run");
                }
                if (!snapshot->topology) {
                    throw std::runtime_error("Missing terrain topology in this DAG run");
                }
                QElapsedTimer oreTimer;
                oreTimer.start();
                handoff->final = std::make_shared<const TerrainSnapshot>(
                    generateOreField(*snapshot));
                handoff->finalToken = "terrain:final:" + std::to_string(handoff->revision);
                ++handoff->stats.oreExecutions;
                qInfo().nospace() << "[Perf][Generation] stage=dag_ore_generate"
                    << " ms=" << oreTimer.nsecsElapsed() / 1000000.0;
                commit.set(
                    outputSlot,
                    handoff->finalToken,
                    proc::v2::WriteLifetime::Persistent,
                    std::string(schema.field_name(outputSlot)));
                return commit;
            });
        return registry;
    }

    proc::ValueStore makeInputStore() const {
        proc::ValueStore init;
        init["generatorIndex"] = proc::make_value(std::to_string(generatorIndex));
        init["seed"] = proc::make_value(std::to_string(params.seed));
        init["seaLevel"] = proc::make_value(std::to_string(params.seaLevel));
        init["scale"] = proc::make_value(QString::number(params.scale, 'g', 9).toStdString());
        init["subdivisionLevel"] = proc::make_value(std::to_string(subdivisionLevel));
        init["geometry"] = proc::make_value(std::to_string(static_cast<int>(geometry)));
        const bool geometryDirty = !handoff->topology || handoff->topology->geometry != geometry
            || handoff->topology->subdivisionLevel != subdivisionLevel;
        init["geometryDirty"] = proc::make_value(geometryDirty ? std::string("1") : std::string("0"));
        init["regeneration"] = proc::make_value(std::to_string(handoff->revision));
        return init;
    }

    TerrainSnapshotHandoff::Snapshot regenerateViaDag() {
        if (!schema || !guardRegistry || !outputs) {
            qWarning() << "DagTerrainBackend runtime is not initialized";
            return {};
        }

        ++handoff->revision;
        handoff->base.reset();
        handoff->final.reset();
        handoff->stats = {};
        const auto inputs = makeInputStore();
        if (!engine) {
            engine = std::make_unique<proc::DefaultDagEngine>(*schema, buildRuntimeRegistry(*schema, handoff, model), *guardRegistry);
            engine->init(inputs);
        } else {
            proc::Commit commit;
            for (const auto& [name, value] : inputs) {
                const auto slot = schema->find_field(name);
                commit.set(*slot, std::string(proc::Commit::debug_view(value)),
                    proc::v2::WriteLifetime::Persistent, name);
            }
            engine->push_input(commit);
        }
        if (!engine->flush_prepare(*outputs)) {
            qWarning() << "DagTerrainBackend flush_prepare failed";
            return {};
        }

        const auto token = proc::get_value_view(engine->prepared_output_store(), "terrainSnapshot");
        if (!token) {
            qWarning() << "DagTerrainBackend produced no terrainSnapshot";
            return {};
        }

        auto snapshot = handoff->resolve(*token);
        if (!snapshot) {
            qWarning() << "DagTerrainBackend failed to resolve terrain snapshot";
            return {};
        }

        if (!engine->ack_outputs()) {
            qWarning() << "DagTerrainBackend ack_outputs failed";
            return {};
        }
        return snapshot;
    }

    void syncFromSnapshot(TerrainSnapshotHandoff::Snapshot snapshot) {
        params = snapshot->params;
        generatorIndex = normalizeTerrainGeneratorIndex(snapshot->generatorIndex);
        subdivisionLevel = snapshot->subdivisionLevel;
        geometry = snapshot->geometry;
        currentSnapshot = std::move(snapshot);
    }

    void syncFromAdapter() {
        if (!bridge) {
            return;
        }
        syncFromSnapshot(std::make_shared<const TerrainSnapshot>(bridge->captureTerrainSnapshot()));
    }
};

DagTerrainBackend::DagTerrainBackend()
    : impl_(std::make_unique<Impl>()) {
}

DagTerrainBackend::~DagTerrainBackend() = default;
DagTerrainBackend::DagTerrainBackend(DagTerrainBackend&&) noexcept = default;
DagTerrainBackend& DagTerrainBackend::operator=(DagTerrainBackend&&) noexcept = default;

void DagTerrainBackend::attachTerrainBridge(ITerrainSceneBridge* bridge) {
    impl_->bridge = bridge;
}

void DagTerrainBackend::initializeTerrainState() {
    impl_->syncFromAdapter();
}

void DagTerrainBackend::setTerrainParams(const TerrainParams& params) {
    impl_->params = params;
}

void DagTerrainBackend::setGeneratorByIndex(int idx) {
    impl_->generatorIndex = normalizeTerrainGeneratorIndex(idx);
}

void DagTerrainBackend::setSubdivisionLevel(int level) {
    impl_->subdivisionLevel = level;
}

void DagTerrainBackend::setGeometry(SphereGeometry geometry) {
    impl_->geometry = geometry;
}

TerrainRegenerationResult DagTerrainBackend::regenerateTerrain() {
    if (!impl_->bridge) {
        return TerrainRegenerationResult::failure("Terrain bridge is not attached");
    }

    TerrainSnapshotHandoff::Snapshot snapshot;
    try { snapshot = impl_->regenerateViaDag(); }
    catch (const std::exception& e) { qWarning() << "Terrain DAG failed:" << e.what(); }
    if (!snapshot) {
        impl_->engine.reset();
        impl_->handoff = std::make_shared<TerrainSnapshotHandoff>();
        qWarning() << "DagTerrainBackend terrain regeneration failed";
        return TerrainRegenerationResult::failure("DAG terrain regeneration failed");
    }

    impl_->syncFromSnapshot(snapshot);
    impl_->bridge->projectTerrainSnapshot(*snapshot);
    return TerrainRegenerationResult::success();
}

const TerrainSnapshot* DagTerrainBackend::currentTerrainSnapshot() const {
    if (!impl_->currentSnapshot) {
        return nullptr;
    }
    return &*impl_->currentSnapshot;
}

TerrainBuildStats DagTerrainBackend::lastBuildStats() const {
    return impl_->handoff->stats;
}

