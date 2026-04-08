#include "DagTerrainBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtDebug>

#include <optional>
#include <stdexcept>
#include <utility>

#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

import Proc;

namespace {

QJsonArray serializeVec3(const QVector3D& value) {
    return QJsonArray{ value.x(), value.y(), value.z() };
}

QVector3D deserializeVec3(const QJsonValue& value) {
    const QJsonArray array = value.toArray();
    if (array.size() != 3) {
        return {};
    }
    return QVector3D(
        static_cast<float>(array[0].toDouble()),
        static_cast<float>(array[1].toDouble()),
        static_cast<float>(array[2].toDouble()));
}

QString serializeTerrainSnapshot(const TerrainSnapshot& snapshot) {
    QJsonObject root;
    root["version"] = 1;
    root["subdivisionLevel"] = snapshot.subdivisionLevel;
    root["generatorIndex"] = snapshot.generatorIndex;
    root["seed"] = static_cast<qint64>(snapshot.params.seed);
    root["seaLevel"] = snapshot.params.seaLevel;
    root["scale"] = snapshot.params.scale;

    QJsonArray cells;
    for (const auto& cell : snapshot.cells) {
        QJsonObject entry;
        entry["height"] = cell.height;
        entry["biome"] = static_cast<int>(cell.biome);
        entry["temperature"] = cell.temperature;
        entry["humidity"] = cell.humidity;
        entry["pressure"] = cell.pressure;
        entry["oreDensity"] = cell.oreDensity;
        entry["oreType"] = static_cast<int>(cell.oreType);
        entry["oreVisualDensity"] = cell.oreVisual.density;
        entry["oreVisualGrainSize"] = cell.oreVisual.grainSize;
        entry["oreVisualGrainContrast"] = cell.oreVisual.grainContrast;
        entry["oreVisualBaseColor"] = serializeVec3(cell.oreVisual.baseColor);
        entry["oreVisualGrainColor"] = serializeVec3(cell.oreVisual.grainColor);
        entry["oreNoiseOffset"] = cell.oreNoiseOffset;
        cells.push_back(entry);
    }
    root["cells"] = cells;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

std::optional<TerrainSnapshot> deserializeTerrainSnapshot(const QString& encoded) {
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8());
    if (!doc.isObject()) {
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    TerrainSnapshot snapshot;
    snapshot.subdivisionLevel = root["subdivisionLevel"].toInt(2);
    snapshot.generatorIndex = root["generatorIndex"].toInt(3);
    snapshot.params.seed = static_cast<uint32_t>(root["seed"].toInteger());
    snapshot.params.seaLevel = root["seaLevel"].toInt();
    snapshot.params.scale = static_cast<float>(root["scale"].toDouble(1.0));

    const QJsonArray cells = root["cells"].toArray();
    snapshot.cells.reserve(static_cast<size_t>(cells.size()));
    for (const auto& value : cells) {
        const QJsonObject entry = value.toObject();
        TerrainCellSnapshot cell;
        cell.height = entry["height"].toInt();
        cell.biome = static_cast<Biome>(entry["biome"].toInt(static_cast<int>(Biome::Grass)));
        cell.temperature = static_cast<float>(entry["temperature"].toDouble());
        cell.humidity = static_cast<float>(entry["humidity"].toDouble());
        cell.pressure = static_cast<float>(entry["pressure"].toDouble());
        cell.oreDensity = static_cast<float>(entry["oreDensity"].toDouble());
        cell.oreType = static_cast<uint8_t>(entry["oreType"].toInt());
        cell.oreVisual.density = static_cast<float>(entry["oreVisualDensity"].toDouble());
        cell.oreVisual.grainSize = static_cast<float>(entry["oreVisualGrainSize"].toDouble(0.05));
        cell.oreVisual.grainContrast = static_cast<float>(entry["oreVisualGrainContrast"].toDouble(1.0));
        cell.oreVisual.baseColor = deserializeVec3(entry["oreVisualBaseColor"]);
        cell.oreVisual.grainColor = deserializeVec3(entry["oreVisualGrainColor"]);
        cell.oreNoiseOffset = static_cast<float>(entry["oreNoiseOffset"].toDouble());
        snapshot.cells.push_back(cell);
    }

    return snapshot;
}

TerrainSnapshot buildTerrainSnapshot(int generatorIndex, int subdivisionLevel, const TerrainParams& params) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(subdivisionLevel));

    auto generator = createTerrainGeneratorByIndex(generatorIndex);
    generator->generate(model, params);

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
        cellSnapshot.oreVisual = cell.oreVisual;
        cellSnapshot.oreNoiseOffset = cell.oreNoiseOffset;
        snapshot.cells.push_back(cellSnapshot);
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
    TerrainParams params{ 12345u, 3, 3.0f };
    int generatorIndex = 3;
    int subdivisionLevel = 2;
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
                {"terrainSnapshot", "str"},
            },
            {
                proc::GraphSchemaBuilder::NodeDef{
                    "TerrainBuild",
                    "buildTerrain",
                    {"generatorIndex", "seed", "seaLevel", "scale", "subdivisionLevel"},
                    {"terrainSnapshot"},
                    std::nullopt,
                },
            },
            proc::make_builtin_operation_registry(),
            proc::make_builtin_algebra_registry());
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

    static proc::RuntimeOperationRegistry buildRuntimeRegistry(const proc::GraphSchema& schema) {
        proc::RuntimeOperationRegistry registry(proc::make_builtin_operation_registry());
        const auto nodeSlot = schema.find_node("TerrainBuild");
        const auto outputSlot = schema.find_field("terrainSnapshot");
        const auto generatorSlot = schema.find_field("generatorIndex");
        const auto seedSlot = schema.find_field("seed");
        const auto seaLevelSlot = schema.find_field("seaLevel");
        const auto scaleSlot = schema.find_field("scale");
        const auto subdivisionSlot = schema.find_field("subdivisionLevel");

        if (!nodeSlot || !outputSlot || !generatorSlot || !seedSlot || !seaLevelSlot || !scaleSlot || !subdivisionSlot) {
            throw std::runtime_error("DagTerrainBackend failed to bind terrain DAG schema slots");
        }

        registry.bind_executor(
            schema.op_of(*nodeSlot),
            *nodeSlot,
            [schema, outputSlot = *outputSlot, generatorSlot = *generatorSlot, seedSlot = *seedSlot,
             seaLevelSlot = *seaLevelSlot, scaleSlot = *scaleSlot, subdivisionSlot = *subdivisionSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                TerrainParams params;
                params.seed = static_cast<uint32_t>(Impl::readIntField(readHandle, fieldName, seedSlot, 0));
                params.seaLevel = Impl::readIntField(readHandle, fieldName, seaLevelSlot, 0);
                params.scale = Impl::readFloatField(readHandle, fieldName, scaleSlot, 1.0f);

                const int generatorIndex = Impl::readIntField(readHandle, fieldName, generatorSlot, 3);
                const int subdivisionLevel = Impl::readIntField(readHandle, fieldName, subdivisionSlot, 2);

                const auto snapshot = buildTerrainSnapshot(generatorIndex, subdivisionLevel, params);

                proc::Commit commit;
                commit.set(
                    outputSlot,
                    serializeTerrainSnapshot(snapshot).toStdString(),
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

    auto snapshot = impl_->regenerateViaDag();
    if (!snapshot) {
        qWarning() << "DagTerrainBackend terrain regeneration failed";
        return TerrainRegenerationResult::failure("DAG terrain regeneration failed");
    }

    impl_->syncFromSnapshot(*snapshot);
    impl_->bridge->projectTerrainSnapshot(*snapshot);
    return TerrainRegenerationResult::success();
}

const TerrainSnapshot* DagTerrainBackend::currentTerrainSnapshot() const {
    if (!impl_->currentSnapshot) {
        return nullptr;
    }
    return &*impl_->currentSnapshot;
}
