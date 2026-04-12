#include "DagTerrainBackend.h"

#include <QtDebug>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include <proc/ProcessDag.h>
#include <proc/Schema.h>

#include "TerrainDagPayloadStore.h"
#include "TerrainDagPipeline.h"
#include "renderers/TerrainVisibility.h"

namespace {

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
            std::initializer_list<proc::Field>{ "terrainSnapshot", "terrainMesh" });
        meshOutputs = std::make_unique<std::vector<proc::Field>>(
            std::initializer_list<proc::Field>{ "terrainMesh" });
        visibilityOutputs = std::make_unique<std::vector<proc::Field>>(
            std::initializer_list<proc::Field>{ "visibleTerrainIndices" });
        dag = std::make_unique<proc::DefaultDagEngine>(*schema, *runtimeRegistry, *guardRegistry);
        dag->init(makeInputStore());
    }

    ITerrainSceneBridge* bridge = nullptr;
    TerrainParams params{ 12345u, 3, 3.0f };
    TerrainRenderConfig renderConfig{};
    int generatorIndex = 3;
    int subdivisionLevel = 2;
    QVector3D cameraPos{ 0.0f, 0.0f, 5.0f };
    std::optional<TerrainSnapshot> currentSnapshot;
    std::string currentTerrainMeshToken;
    std::string currentVisibleIndicesToken;
    uint64_t terrainBuildCount = 0;
    uint64_t meshBuildCount = 0;
    uint64_t visibilityBuildCount = 0;

    TerrainDagPayloadStore payloadStore;

    std::unique_ptr<proc::GraphSchema> schema;
    std::unique_ptr<proc::RuntimeOperationRegistry> runtimeRegistry;
    std::unique_ptr<proc::GuardRegistry> guardRegistry;
    std::unique_ptr<proc::DefaultDagEngine> dag;
    std::unique_ptr<std::vector<proc::Field>> terrainOutputs;
    std::unique_ptr<std::vector<proc::Field>> meshOutputs;
    std::unique_ptr<std::vector<proc::Field>> visibilityOutputs;

    static proc::GraphSchema buildSchema() {
        proc::GraphSchema::StorageLayout roles;
        roles.inputs.insert("generatorIndex");
        roles.inputs.insert("seed");
        roles.inputs.insert("seaLevel");
        roles.inputs.insert("scale");
        roles.inputs.insert("subdivisionLevel");
        roles.inputs.insert("terrainRenderInset");
        roles.inputs.insert("terrainRenderSmoothOneStep");
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
                {"terrainRenderInset", "scalar"},
                {"terrainRenderSmoothOneStep", "int"},
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
                    {"terrainSnapshot", "terrainRenderInset", "terrainRenderSmoothOneStep"},
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

    proc::RuntimeOperationRegistry buildRuntimeRegistry(const proc::GraphSchema& schemaRef) {
        proc::RuntimeOperationRegistry registry(makeTerrainDagOperationRegistry());
        const auto terrainNodeSlot = schemaRef.find_node("TerrainBuild");
        const auto meshNodeSlot = schemaRef.find_node("TerrainMeshBuild");
        const auto visibilityNodeSlot = schemaRef.find_node("TerrainVisibility");
        const auto snapshotOutputSlot = schemaRef.find_field("terrainSnapshot");
        const auto meshOutputSlot = schemaRef.find_field("terrainMesh");
        const auto visibilityOutputSlot = schemaRef.find_field("visibleTerrainIndices");
        const auto generatorSlot = schemaRef.find_field("generatorIndex");
        const auto seedSlot = schemaRef.find_field("seed");
        const auto seaLevelSlot = schemaRef.find_field("seaLevel");
        const auto scaleSlot = schemaRef.find_field("scale");
        const auto subdivisionSlot = schemaRef.find_field("subdivisionLevel");
        const auto renderInsetSlot = schemaRef.find_field("terrainRenderInset");
        const auto renderSmoothSlot = schemaRef.find_field("terrainRenderSmoothOneStep");
        const auto cameraSlot = schemaRef.find_field("cameraPos");
        const auto meshSlot = schemaRef.find_field("terrainMesh");

        if (!terrainNodeSlot || !meshNodeSlot || !visibilityNodeSlot || !snapshotOutputSlot ||
            !meshOutputSlot || !visibilityOutputSlot || !generatorSlot || !seedSlot ||
            !seaLevelSlot || !scaleSlot || !subdivisionSlot || !renderInsetSlot ||
            !renderSmoothSlot || !cameraSlot || !meshSlot) {
            throw std::runtime_error("DagTerrainBackend failed to bind terrain DAG schema slots");
        }

        registry.bind_executor(
            schemaRef.op_of(*terrainNodeSlot),
            *terrainNodeSlot,
            [this,
             &schemaRef,
             snapshotOutputSlot = *snapshotOutputSlot,
             generatorSlot = *generatorSlot,
             seedSlot = *seedSlot,
             seaLevelSlot = *seaLevelSlot,
             scaleSlot = *scaleSlot,
             subdivisionSlot = *subdivisionSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                TerrainParams nextParams;
                nextParams.seed = static_cast<uint32_t>(Impl::readIntField(readHandle, fieldName, seedSlot, 0));
                nextParams.seaLevel = Impl::readIntField(readHandle, fieldName, seaLevelSlot, 0);
                nextParams.scale = Impl::readFloatField(readHandle, fieldName, scaleSlot, 1.0f);

                const int nextGeneratorIndex = Impl::readIntField(readHandle, fieldName, generatorSlot, 3);
                const int nextSubdivisionLevel = Impl::readIntField(readHandle, fieldName, subdivisionSlot, 2);
                const auto snapshot = terrain_dag::buildTerrainSnapshot(nextGeneratorIndex, nextSubdivisionLevel, nextParams);
                ++terrainBuildCount;

                proc::Commit commit;
                commit.set(
                    snapshotOutputSlot,
                    terrain_dag::serializeTerrainSnapshot(snapshot).toStdString(),
                    proc::v2::WriteLifetime::Persistent,
                    std::string(schemaRef.field_name(snapshotOutputSlot)));
                return commit;
            });

        registry.bind_executor(
            schemaRef.op_of(*meshNodeSlot),
            *meshNodeSlot,
            [this,
             &schemaRef,
             snapshotOutputSlot = *snapshotOutputSlot,
             meshOutputSlot = *meshOutputSlot,
             renderInsetSlot = *renderInsetSlot,
             renderSmoothSlot = *renderSmoothSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                const auto encodedSnapshot = proc::Commit::debug_view(readHandle(snapshotOutputSlot));
                const auto snapshot = terrain_dag::deserializeTerrainSnapshot(
                    QString::fromUtf8(encodedSnapshot.data(), static_cast<qsizetype>(encodedSnapshot.size())));

                proc::Commit commit;
                if (snapshot) {
                    TerrainRenderConfig nextRenderConfig;
                    nextRenderConfig.inset = Impl::readFloatField(readHandle, fieldName, renderInsetSlot, 0.25f);
                    nextRenderConfig.smoothOneStep = Impl::readIntField(readHandle, fieldName, renderSmoothSlot, 1) != 0;
                    currentTerrainMeshToken = payloadStore.putTerrainMesh(
                        terrain_dag::buildTerrainMeshFromSnapshot(*snapshot, nextRenderConfig));
                    ++meshBuildCount;
                    commit.set(
                        meshOutputSlot,
                        currentTerrainMeshToken,
                        proc::v2::WriteLifetime::Persistent,
                        std::string(schemaRef.field_name(meshOutputSlot)));
                }
                return commit;
            });

        registry.bind_executor(
            schemaRef.op_of(*visibilityNodeSlot),
            *visibilityNodeSlot,
            [this,
             &schemaRef,
             meshSlot = *meshSlot,
             cameraSlot = *cameraSlot,
             visibilityOutputSlot = *visibilityOutputSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn&,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                const auto meshTokenView = proc::Commit::debug_view(readHandle(meshSlot));
                const auto* mesh = payloadStore.findTerrainMesh(std::string(meshTokenView));
                if (!mesh) {
                    return {};
                }

                const auto encodedCamera = proc::Commit::debug_view(readHandle(cameraSlot));
                cameraPos = terrain_dag::deserializeCameraPos(encodedCamera);
                currentVisibleIndicesToken = payloadStore.putVisibleIndices(
                    buildVisibleTerrainIndices(*mesh, cameraPos));
                ++visibilityBuildCount;

                proc::Commit commit;
                commit.set(
                    visibilityOutputSlot,
                    currentVisibleIndicesToken,
                    proc::v2::WriteLifetime::Persistent,
                    std::string(schemaRef.field_name(visibilityOutputSlot)));
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
        init["terrainRenderInset"] = proc::make_value(QString::number(renderConfig.inset, 'g', 9).toStdString());
        init["terrainRenderSmoothOneStep"] = proc::make_value(renderConfig.smoothOneStep ? "1" : "0");
        init["cameraPos"] = proc::make_value(terrain_dag::serializeCameraPos(cameraPos).toStdString());
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

    void pushTerrainRenderConfigInput() {
        proc::Commit input;
        input.set(proc::Field("terrainRenderInset"), QString::number(renderConfig.inset, 'g', 9).toStdString());
        input.set(proc::Field("terrainRenderSmoothOneStep"), renderConfig.smoothOneStep ? "1" : "0");
        dag->push_input(input);
    }

    void pushCameraInput(const QVector3D& nextCameraPos) {
        cameraPos = nextCameraPos;
        proc::Commit input;
        input.set(proc::Field("cameraPos"), terrain_dag::serializeCameraPos(cameraPos).toStdString());
        dag->push_input(input);
    }

    std::optional<TerrainSnapshot> regenerateViaDag() {
        if (!schema || !runtimeRegistry || !guardRegistry || !terrainOutputs || !dag) {
            qWarning() << "DagTerrainBackend runtime is not initialized";
            return std::nullopt;
        }

        pushTerrainInputs();
        pushTerrainRenderConfigInput();
        if (!dag->flush_prepare(*terrainOutputs)) {
            qWarning() << "DagTerrainBackend flush_prepare failed";
            return std::nullopt;
        }

        const auto encoded = proc::get_value_view(dag->prepared_output_store(), "terrainSnapshot");
        if (!encoded) {
            qWarning() << "DagTerrainBackend produced no terrainSnapshot";
            return std::nullopt;
        }

        auto snapshot = terrain_dag::deserializeTerrainSnapshot(
            QString::fromUtf8(encoded->data(), static_cast<qsizetype>(encoded->size())));
        if (!snapshot) {
            qWarning() << "DagTerrainBackend failed to decode terrain snapshot";
            return std::nullopt;
        }

        if (!dag->ack_outputs()) {
            qWarning() << "DagTerrainBackend ack_outputs failed";
        }
        return snapshot;
    }

    bool prepareTerrainMeshViaDag() {
        if (!meshOutputs || !dag) {
            return false;
        }

        pushTerrainRenderConfigInput();
        if (!dag->flush_prepare(*meshOutputs)) {
            return currentTerrainMesh() != nullptr;
        }

        const bool ok = proc::get_value_view(dag->prepared_output_store(), "terrainMesh").has_value();
        if (!dag->ack_outputs()) {
            qWarning() << "DagTerrainBackend terrain mesh ack_outputs failed";
        }
        return ok && currentTerrainMesh() != nullptr;
    }

    bool prepareVisibleIndicesViaDag(const QVector3D& nextCameraPos) {
        if (!visibilityOutputs || !dag) {
            return false;
        }

        pushTerrainRenderConfigInput();
        pushCameraInput(nextCameraPos);
        if (!dag->flush_prepare(*visibilityOutputs)) {
            return currentVisibleIndices() != nullptr;
        }

        const bool ok = proc::get_value_view(dag->prepared_output_store(), "visibleTerrainIndices").has_value();
        if (!dag->ack_outputs()) {
            qWarning() << "DagTerrainBackend visibility ack_outputs failed";
        }
        return ok && currentVisibleIndices() != nullptr;
    }

    const TerrainMesh* currentTerrainMesh() const {
        if (currentTerrainMeshToken.empty()) {
            return nullptr;
        }
        return payloadStore.findTerrainMesh(currentTerrainMeshToken);
    }

    const std::vector<uint32_t>* currentVisibleIndices() const {
        if (currentVisibleIndicesToken.empty()) {
            return nullptr;
        }
        return payloadStore.findVisibleIndices(currentVisibleIndicesToken);
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

void DagTerrainBackend::setTerrainRenderConfig(const TerrainRenderConfig& config) {
    impl_->renderConfig = config;
    impl_->renderConfig.inset = std::clamp(impl_->renderConfig.inset, 0.0f, 0.49f);
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

bool DagTerrainBackend::prepareTerrainMesh() {
    return impl_->prepareTerrainMeshViaDag();
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
    return impl_->currentTerrainMesh();
}

const std::vector<uint32_t>* DagTerrainBackend::currentVisibleTerrainIndices() const {
    return impl_->currentVisibleIndices();
}

TerrainDagStats DagTerrainBackend::debugStats() const {
    const auto* visibleIndices = impl_->currentVisibleIndices();
    return TerrainDagStats{
        impl_->terrainBuildCount,
        impl_->meshBuildCount,
        impl_->visibilityBuildCount,
        visibleIndices ? visibleIndices->size() : 0,
    };
}
