#include "DagTerrainBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtDebug>

#include <optional>
#include <stdexcept>
#include <sstream>
#include <utility>

#include "generation/TerrainGenerator.h"
#include "generation/MeshGenerators/TerrainMeshGenerator.h"
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

float terrainHeightStepForSubdivision(int subdivisionLevel) {
    return 0.05f / (1.0f + subdivisionLevel * 0.4f);
}

TerrainMesh buildTerrainMeshFromSnapshot(const TerrainSnapshot& snapshot) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(snapshot.subdivisionLevel));

    auto& cells = model.cells();
    const size_t count = std::min(cells.size(), snapshot.cells.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& source = snapshot.cells[i];
        auto& target = cells[i];
        target.height = source.height;
        target.biome = source.biome;
        target.temperature = source.temperature;
        target.humidity = source.humidity;
        target.pressure = source.pressure;
        target.oreDensity = source.oreDensity;
        target.oreType = source.oreType;
        target.oreVisual = source.oreVisual;
        target.oreNoiseOffset = source.oreNoiseOffset;
    }

    TerrainMeshOptions options;
    options.heightStep = terrainHeightStepForSubdivision(snapshot.subdivisionLevel);
    return TerrainMeshGenerator::buildTerrainMesh(model, options);
}

QString serializeCameraPos(const QVector3D& cameraPos) {
    return QString("%1,%2,%3")
        .arg(QString::number(cameraPos.x(), 'g', 9))
        .arg(QString::number(cameraPos.y(), 'g', 9))
        .arg(QString::number(cameraPos.z(), 'g', 9));
}

QVector3D deserializeCameraPos(std::string_view encoded) {
    const QString text = QString::fromUtf8(encoded.data(), static_cast<qsizetype>(encoded.size()));
    const QStringList parts = text.split(',');
    if (parts.size() != 3) {
        return QVector3D(0, 0, 5);
    }

    return QVector3D(
        parts[0].toFloat(),
        parts[1].toFloat(),
        parts[2].toFloat());
}

std::vector<uint32_t> buildVisibleTerrainIndices(const TerrainMesh& mesh, const QVector3D& cameraPos) {
    static constexpr float kVisibilityDotMargin = -0.05f;

    const QVector3D toCamVector = cameraPos;
    if (toCamVector.lengthSquared() <= 1.0e-8f) {
        return mesh.idx;
    }
    const QVector3D toCam = toCamVector.normalized();

    std::vector<uint32_t> visible;
    visible.reserve(mesh.idx.size() / 2);
    for (size_t i = 0; i + 2 < mesh.idx.size(); i += 3) {
        const auto vertexAt = [&mesh](uint32_t index) {
            const size_t base = static_cast<size_t>(index) * 3;
            return QVector3D(mesh.pos[base], mesh.pos[base + 1], mesh.pos[base + 2]);
        };

        const uint32_t i0 = mesh.idx[i];
        const uint32_t i1 = mesh.idx[i + 1];
        const uint32_t i2 = mesh.idx[i + 2];
        const QVector3D center = (vertexAt(i0) + vertexAt(i1) + vertexAt(i2)) * (1.0f / 3.0f);
        if (QVector3D::dotProduct(center.normalized(), toCam) > kVisibilityDotMargin) {
            visible.push_back(i0);
            visible.push_back(i1);
            visible.push_back(i2);
        }
    }

    return visible;
}

std::string summarizeIndices(const std::vector<uint32_t>& indices) {
    return "indices:" + std::to_string(indices.size());
}

proc::OperationRegistry makeTerrainDagOperationRegistry() {
    proc::OperationRegistry registry = proc::make_builtin_operation_registry();
    registry.register_op("buildTerrainMesh", 1001);
    registry.register_op("computeTerrainVisibility", 1002);
    return registry;
}

} // namespace

struct DagTerrainBackend::Impl {
    Impl() {
        schema = std::make_unique<proc::GraphSchema>(buildSchema());
        runtimeRegistry = std::make_unique<proc::RuntimeOperationRegistry>(buildRuntimeRegistry(*schema));
        guardRegistry = std::make_unique<proc::GuardRegistry>(proc::make_builtin_guard_registry());
        terrainOutputs = std::make_unique<std::vector<proc::Field>>(
            std::initializer_list<proc::Field>{ "terrainSnapshot", "visibleTerrainIndices" });
        visibilityOutputs = std::make_unique<std::vector<proc::Field>>(
            std::initializer_list<proc::Field>{ "visibleTerrainIndices" });
        dag = std::make_unique<proc::DefaultDagEngine>(*schema, *runtimeRegistry, *guardRegistry);
        dag->init(makeInputStore());
    }

    ITerrainSceneBridge* bridge = nullptr;
    TerrainParams params{ 12345u, 3, 3.0f };
    int generatorIndex = 3;
    int subdivisionLevel = 2;
    QVector3D cameraPos{ 0, 0, 5 };
    std::optional<TerrainSnapshot> currentSnapshot;
    TerrainMesh currentVisibilityMesh;
    std::vector<uint32_t> currentVisibleIndices;
    uint64_t terrainBuildCount = 0;
    uint64_t meshBuildCount = 0;
    uint64_t visibilityBuildCount = 0;

    std::unique_ptr<proc::GraphSchema> schema;
    std::unique_ptr<proc::RuntimeOperationRegistry> runtimeRegistry;
    std::unique_ptr<proc::GuardRegistry> guardRegistry;
    std::unique_ptr<proc::DefaultDagEngine> dag;
    std::unique_ptr<std::vector<proc::Field>> terrainOutputs;
    std::unique_ptr<std::vector<proc::Field>> visibilityOutputs;

    static proc::GraphSchema buildSchema() {
        proc::GraphSchema::StorageLayout roles;
        roles.inputs.insert("generatorIndex");
        roles.inputs.insert("seed");
        roles.inputs.insert("seaLevel");
        roles.inputs.insert("scale");
        roles.inputs.insert("subdivisionLevel");
        roles.inputs.insert("cameraPos");
        roles.outputs.insert("terrainSnapshot");
        roles.outputs.insert("terrainMesh");
        roles.outputs.insert("visibleTerrainIndices");

        return proc::GraphSchemaBuilder::compile(
            roles,
            {
                {"generatorIndex", "int"},
                {"seed", "int"},
                {"seaLevel", "int"},
                {"scale", "scalar"},
                {"subdivisionLevel", "int"},
                {"cameraPos", "str"},
                {"terrainSnapshot", "str"},
                {"terrainMesh", "str"},
                {"visibleTerrainIndices", "str"},
            },
            {
                proc::GraphSchemaBuilder::NodeDef{
                    "TerrainBuild",
                    "buildTerrain",
                    {"generatorIndex", "seed", "seaLevel", "scale", "subdivisionLevel"},
                    {"terrainSnapshot"},
                    std::nullopt,
                },
                proc::GraphSchemaBuilder::NodeDef{
                    "TerrainMeshBuild",
                    "buildTerrainMesh",
                    {"terrainSnapshot"},
                    {"terrainMesh"},
                    std::nullopt,
                },
                proc::GraphSchemaBuilder::NodeDef{
                    "TerrainVisibility",
                    "computeTerrainVisibility",
                    {"terrainMesh", "cameraPos"},
                    {"visibleTerrainIndices"},
                    std::nullopt,
                },
            },
            makeTerrainDagOperationRegistry(),
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

    proc::RuntimeOperationRegistry buildRuntimeRegistry(const proc::GraphSchema& schema) {
        proc::RuntimeOperationRegistry registry(makeTerrainDagOperationRegistry());
        const auto nodeSlot = schema.find_node("TerrainBuild");
        const auto meshNodeSlot = schema.find_node("TerrainMeshBuild");
        const auto visibilityNodeSlot = schema.find_node("TerrainVisibility");
        const auto outputSlot = schema.find_field("terrainSnapshot");
        const auto meshOutputSlot = schema.find_field("terrainMesh");
        const auto visibilityOutputSlot = schema.find_field("visibleTerrainIndices");
        const auto generatorSlot = schema.find_field("generatorIndex");
        const auto seedSlot = schema.find_field("seed");
        const auto seaLevelSlot = schema.find_field("seaLevel");
        const auto scaleSlot = schema.find_field("scale");
        const auto subdivisionSlot = schema.find_field("subdivisionLevel");
        const auto cameraSlot = schema.find_field("cameraPos");
        const auto meshSlot = schema.find_field("terrainMesh");

        if (!nodeSlot || !meshNodeSlot || !visibilityNodeSlot || !outputSlot || !meshOutputSlot || !visibilityOutputSlot ||
            !generatorSlot || !seedSlot || !seaLevelSlot || !scaleSlot || !subdivisionSlot || !cameraSlot || !meshSlot) {
            throw std::runtime_error("DagTerrainBackend failed to bind terrain DAG schema slots");
        }

        registry.bind_executor(
            schema.op_of(*nodeSlot),
            *nodeSlot,
            [this, schema, outputSlot = *outputSlot, generatorSlot = *generatorSlot, seedSlot = *seedSlot,
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
                ++terrainBuildCount;

                proc::Commit commit;
                commit.set(
                    outputSlot,
                    serializeTerrainSnapshot(snapshot).toStdString(),
                    proc::v2::WriteLifetime::Persistent,
                    std::string(schema.field_name(outputSlot)));
                return commit;
            });
        registry.bind_executor(
            schema.op_of(*meshNodeSlot),
            *meshNodeSlot,
            [this, schema, outputSlot = *outputSlot, meshOutputSlot = *meshOutputSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn&,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                const auto encodedSnapshot = proc::Commit::debug_view(readHandle(outputSlot));
                const auto snapshot = deserializeTerrainSnapshot(
                    QString::fromUtf8(encodedSnapshot.data(), static_cast<qsizetype>(encodedSnapshot.size())));

                proc::Commit commit;
                if (snapshot) {
                    currentVisibilityMesh = buildTerrainMeshFromSnapshot(*snapshot);
                    ++meshBuildCount;
                    commit.set(
                        meshOutputSlot,
                        "terrainMesh:" + std::to_string(meshBuildCount),
                        proc::v2::WriteLifetime::Persistent,
                        std::string(schema.field_name(meshOutputSlot)));
                }
                return commit;
            });
        registry.bind_executor(
            schema.op_of(*visibilityNodeSlot),
            *visibilityNodeSlot,
            [this, schema, meshSlot = *meshSlot, cameraSlot = *cameraSlot, visibilityOutputSlot = *visibilityOutputSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn&,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                (void)readHandle(meshSlot);
                const auto encodedCamera = proc::Commit::debug_view(readHandle(cameraSlot));
                cameraPos = deserializeCameraPos(encodedCamera);
                currentVisibleIndices = buildVisibleTerrainIndices(currentVisibilityMesh, cameraPos);
                ++visibilityBuildCount;

                proc::Commit commit;
                commit.set(
                    visibilityOutputSlot,
                    summarizeIndices(currentVisibleIndices),
                    proc::v2::WriteLifetime::Persistent,
                    std::string(schema.field_name(visibilityOutputSlot)));
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
        init["cameraPos"] = proc::make_value(serializeCameraPos(cameraPos).toStdString());
        return init;
    }

    void pushTerrainInputs() {
        proc::Commit input;
        input.set(proc::Field("generatorIndex"), std::to_string(generatorIndex));
        input.set(proc::Field("seed"), std::to_string(params.seed));
        input.set(proc::Field("seaLevel"), std::to_string(params.seaLevel));
        input.set(proc::Field("scale"), QString::number(params.scale, 'g', 9).toStdString());
        input.set(proc::Field("subdivisionLevel"), std::to_string(subdivisionLevel));
        dag->push_input(input);
    }

    void pushCameraInput(const QVector3D& nextCameraPos) {
        cameraPos = nextCameraPos;
        proc::Commit input;
        input.set(proc::Field("cameraPos"), serializeCameraPos(cameraPos).toStdString());
        dag->push_input(input);
    }

    std::optional<TerrainSnapshot> regenerateViaDag() {
        if (!schema || !runtimeRegistry || !guardRegistry || !terrainOutputs || !dag) {
            qWarning() << "DagTerrainBackend runtime is not initialized";
            return std::nullopt;
        }

        pushTerrainInputs();
        if (!dag->flush_prepare(*terrainOutputs)) {
            qWarning() << "DagTerrainBackend flush_prepare failed";
            return std::nullopt;
        }

        const auto encoded = proc::get_value_view(dag->prepared_output_store(), "terrainSnapshot");
        if (!encoded) {
            qWarning() << "DagTerrainBackend produced no terrainSnapshot";
            return std::nullopt;
        }

        auto snapshot = deserializeTerrainSnapshot(QString::fromUtf8(encoded->data(), static_cast<qsizetype>(encoded->size())));
        if (!snapshot) {
            qWarning() << "DagTerrainBackend failed to decode terrain snapshot";
            return std::nullopt;
        }

        if (!dag->ack_outputs()) {
            qWarning() << "DagTerrainBackend ack_outputs failed";
        }
        return snapshot;
    }

    bool prepareVisibleIndicesViaDag(const QVector3D& nextCameraPos) {
        if (!visibilityOutputs || !dag) {
            return false;
        }

        pushCameraInput(nextCameraPos);
        if (!dag->flush_prepare(*visibilityOutputs)) {
            return !currentVisibleIndices.empty();
        }

        const bool ok = proc::get_value_view(dag->prepared_output_store(), "visibleTerrainIndices").has_value();
        if (!dag->ack_outputs()) {
            qWarning() << "DagTerrainBackend visibility ack_outputs failed";
        }
        return ok && !currentVisibleIndices.empty();
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

void DagTerrainBackend::setVisibilityMesh(const TerrainMesh& mesh) {
    impl_->currentVisibilityMesh = mesh;
    impl_->pushCameraInput(impl_->cameraPos);
}

bool DagTerrainBackend::prepareVisibleTerrainIndices(const QVector3D& cameraPos) {
    return impl_->prepareVisibleIndicesViaDag(cameraPos);
}

const TerrainSnapshot* DagTerrainBackend::currentTerrainSnapshot() const {
    if (!impl_->currentSnapshot) {
        return nullptr;
    }
    return &*impl_->currentSnapshot;
}

const TerrainMesh* DagTerrainBackend::currentTerrainMesh() const {
    if (impl_->currentVisibilityMesh.idx.empty()) {
        return nullptr;
    }
    return &impl_->currentVisibilityMesh;
}

const std::vector<uint32_t>* DagTerrainBackend::currentVisibleTerrainIndices() const {
    if (impl_->currentVisibleIndices.empty()) {
        return nullptr;
    }
    return &impl_->currentVisibleIndices;
}

TerrainDagStats DagTerrainBackend::debugStats() const {
    return TerrainDagStats{
        impl_->terrainBuildCount,
        impl_->meshBuildCount,
        impl_->visibilityBuildCount,
        impl_->currentVisibleIndices.size(),
    };
}
