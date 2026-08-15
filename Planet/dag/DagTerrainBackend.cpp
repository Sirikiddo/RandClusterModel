#include "DagTerrainBackend.h"

#include <QElapsedTimer>
#include <QtDebug>

#include <optional>
#include <algorithm>
#include <stdexcept>
#include <utility>

#include "generation/TerrainGenerator.h"
#include "generation/OreGenerator.h"
#include "model/HexSphereModel.h"
#include "TerrainSerialization.h"

#include <proc/ProcessDag.h>
#include <proc/Schema.h>
#include <proc/Logging.h>
#include <proc/FileSchema.h>
#include <proc/ScenarioReader.h>

namespace {
TerrainSnapshot buildBaseTerrainSnapshot(int generatorIndex, int subdivisionLevel, const TerrainParams& params) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(subdivisionLevel));

    auto generator = createTerrainGeneratorByIndex(generatorIndex);
    generateCanonicalTerrain(*generator, model, params);

    TerrainSnapshot snapshot;
    snapshot.generatorIndex = normalizeTerrainGeneratorIndex(generatorIndex);
    snapshot.subdivisionLevel = subdivisionLevel;
    snapshot.params = params;
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

    return snapshot;
}

TerrainSnapshot generateOreField(TerrainSnapshot snapshot) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(snapshot.subdivisionLevel));
    auto& cells = model.cells();
    const size_t count = std::min(cells.size(), snapshot.cells.size());
    for (size_t i = 0; i < count; ++i) {
        cells[i].height = snapshot.cells[i].height;
        cells[i].biome = snapshot.cells[i].biome;
    }

    if (normalizeTerrainGeneratorIndex(snapshot.generatorIndex) == 0) {
        OreGenerator::clear(model);
    }
    else {
        OreGenerator::generate(model, snapshot.params.seed);
    }

    for (size_t i = 0; i < count; ++i) {
        snapshot.cells[i].oreDensity = cells[i].oreDensity;
        snapshot.cells[i].oreType = cells[i].oreType;
    }
    return snapshot;
}

} // namespace

struct DagTerrainBackend::Impl {
    Impl() {
        schema = std::make_unique<proc::GraphSchema>(buildSchema());
        runtimeRegistry = std::make_unique<proc::RuntimeOperationRegistry>(buildRuntimeRegistry(*schema));
        guardRegistry = std::make_unique<proc::GuardRegistry>(proc::make_builtin_guard_registry());
        outputs = std::make_unique<std::vector<proc::Field>>(std::initializer_list<proc::Field>{ "terrainSnapshot" });
    }

    ITerrainSceneBridge* bridge = nullptr;
    TerrainParams params = defaultTerrainParams();
    int generatorIndex = kDefaultTerrainGeneratorIndex;
    int subdivisionLevel = kDefaultTerrainSubdivisionLevel;
    std::optional<TerrainSnapshot> currentSnapshot;

    std::unique_ptr<proc::GraphSchema> schema;
    std::unique_ptr<proc::RuntimeOperationRegistry> runtimeRegistry;
    std::unique_ptr<proc::GuardRegistry> guardRegistry;
    std::unique_ptr<std::vector<proc::Field>> outputs;

    static proc::GraphSchema buildSchema() {
        proc::GraphSchema::StorageLayout roles;
        roles.inputs.insert("generatorIndex");
        roles.inputs.insert("seed");
        roles.inputs.insert("seaLevel");
        roles.inputs.insert("scale");
        roles.inputs.insert("subdivisionLevel");
        roles.outputs.insert("terrainSnapshot");

        return proc::GraphSchemaBuilder::compile(
            roles,
            {
                {"generatorIndex", "int"},
                {"seed", "int"},
                {"seaLevel", "int"},
                {"scale", "scalar"},
                {"subdivisionLevel", "int"},
                {"baseTerrainSnapshot", "str"},
                {"terrainSnapshot", "str"},
            },
            {
                proc::GraphSchemaBuilder::NodeDef{
                    "TerrainBuild",
                    "buildTerrain",
                    {"generatorIndex", "seed", "seaLevel", "scale", "subdivisionLevel"},
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

    static std::string readStringField(
        const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
        proc::v2::FieldSlot slot) {
        const auto handle = readHandle(slot);
        const auto view = proc::Commit::debug_view(handle);
        return std::string(view.data(), view.size());
    }

    static proc::RuntimeOperationRegistry buildRuntimeRegistry(const proc::GraphSchema& schema) {
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

        if (!nodeSlot || !baseOutputSlot || !oreNodeSlot || !outputSlot || !generatorSlot || !seedSlot || !seaLevelSlot || !scaleSlot || !subdivisionSlot) {
            throw std::runtime_error("DagTerrainBackend failed to bind terrain DAG schema slots");
        }

        registry.bind_executor(
            schema.op_of(*nodeSlot),
            *nodeSlot,
            [schema, outputSlot = *baseOutputSlot, generatorSlot = *generatorSlot, seedSlot = *seedSlot,
             seaLevelSlot = *seaLevelSlot, scaleSlot = *scaleSlot, subdivisionSlot = *subdivisionSlot](
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
                const int subdivisionLevel = Impl::readIntField(
                    readHandle, fieldName, subdivisionSlot, kDefaultTerrainSubdivisionLevel);

                const auto snapshot = buildBaseTerrainSnapshot(generatorIndex, subdivisionLevel, params);

                proc::Commit commit;
                commit.set(
                    outputSlot,
                    serializeTerrainSnapshot(snapshot).toStdString(),
                    proc::v2::WriteLifetime::Persistent,
                    std::string(schema.field_name(outputSlot)));
                return commit;
            });
        registry.bind_executor(
            schema.op_of(*oreNodeSlot),
            *oreNodeSlot,
            [schema, baseSlot = *baseOutputSlot, outputSlot = *outputSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn&,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                proc::Commit commit;
                const auto encoded = Impl::readStringField(readHandle, baseSlot);
                const auto snapshot = deserializeTerrainSnapshot(QString::fromUtf8(encoded.data(), static_cast<qsizetype>(encoded.size())));
                if (!snapshot) {
                    return commit;
                }
                const TerrainSnapshot withOre = generateOreField(*snapshot);
                commit.set(
                    outputSlot,
                    serializeTerrainSnapshot(withOre).toStdString(),
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
        return init;
    }

    std::optional<TerrainSnapshot> regenerateViaDag() const {
        if (!schema || !runtimeRegistry || !guardRegistry || !outputs) {
            qWarning() << "DagTerrainBackend runtime is not initialized";
            return std::nullopt;
        }

        proc::DefaultDagEngine dag(*schema, *runtimeRegistry, *guardRegistry);
        dag.init(makeInputStore());
        if (!dag.flush_prepare(*outputs)) {
            qWarning() << "DagTerrainBackend flush_prepare failed";
            return std::nullopt;
        }

        const auto encoded = proc::get_value_view(dag.prepared_output_store(), "terrainSnapshot");
        if (!encoded) {
            qWarning() << "DagTerrainBackend produced no terrainSnapshot";
            return std::nullopt;
        }

        auto snapshot = deserializeTerrainSnapshot(QString::fromUtf8(encoded->data(), static_cast<qsizetype>(encoded->size())));
        if (!snapshot) {
            qWarning() << "DagTerrainBackend failed to decode terrain snapshot";
            return std::nullopt;
        }

        if (!dag.ack_outputs()) {
            qWarning() << "DagTerrainBackend ack_outputs failed";
        }
        return snapshot;
    }

    void syncFromSnapshot(const TerrainSnapshot& snapshot) {
        params = snapshot.params;
        generatorIndex = normalizeTerrainGeneratorIndex(snapshot.generatorIndex);
        subdivisionLevel = snapshot.subdivisionLevel;
        currentSnapshot = snapshot;
    }

    void syncFromAdapter() {
        if (!bridge) {
            return;
        }
        syncFromSnapshot(bridge->captureTerrainSnapshot());
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

TerrainRegenerationResult DagTerrainBackend::regenerateTerrain() {
    if (!impl_->bridge) {
        return TerrainRegenerationResult::failure("Terrain bridge is not attached");
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer stageTimer;
    stageTimer.start();
    auto snapshot = impl_->regenerateViaDag();
    const double dagMs = stageTimer.nsecsElapsed() / 1000000.0;
    if (!snapshot) {
        qWarning() << "DagTerrainBackend terrain regeneration failed";
        return TerrainRegenerationResult::failure("DAG terrain regeneration failed");
    }

    impl_->syncFromSnapshot(*snapshot);
    stageTimer.restart();
    impl_->bridge->projectTerrainSnapshot(*snapshot);
    const double projectionMs = stageTimer.nsecsElapsed() / 1000000.0;
    qInfo().nospace()
        << "[Perf][Generation] stage=terrain_backend_total level=" << snapshot->subdivisionLevel
        << " cells=" << snapshot->cells.size()
        << " dag_ms=" << dagMs
        << " projection_ms=" << projectionMs
        << " total_ms=" << totalTimer.nsecsElapsed() / 1000000.0;
    return TerrainRegenerationResult::success();
}

const TerrainSnapshot* DagTerrainBackend::currentTerrainSnapshot() const {
    if (!impl_->currentSnapshot) {
        return nullptr;
    }
    return &*impl_->currentSnapshot;
}

