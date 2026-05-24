#include "DagSceneBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtDebug>

#include <algorithm>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "TerrainSerialization.h"
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"

#include <proc/ProcessDag.h>
#include <proc/Schema.h>
#include <proc/Logging.h>

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

std::string readStringField(
    const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
    proc::v2::FieldSlot slot) {
    const auto handle = readHandle(slot);
    const auto debugView = proc::Commit::debug_view(handle);
    return std::string(debugView.data(), debugView.size());
}

QString compactJson(const QJsonObject& root) {
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QString compactJson(const QJsonArray& root) {
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

std::vector<int> deserializeSelectedCells(const QString& encoded) {
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8());
    std::vector<int> result;
    if (!doc.isArray()) {
        return result;
    }
    const QJsonArray array = doc.array();
    result.reserve(static_cast<size_t>(array.size()));
    for (const auto& value : array) {
        result.push_back(value.toInt());
    }
    return result;
}

QString serializeSelectedCells(const std::vector<int>& cells) {
    QJsonArray array;
    for (int cell : cells) {
        array.push_back(cell);
    }
    return compactJson(array);
}

struct VisualParams {
    float heightStep = 0.0f;
    float outlineBias = 0.0f;
    bool smoothOneStep = true;
};

QString serializeVisualParams(const VisualParams& params) {
    QJsonObject root;
    root["heightStep"] = params.heightStep;
    root["outlineBias"] = params.outlineBias;
    root["smoothOneStep"] = params.smoothOneStep;
    return compactJson(root);
}

VisualParams deserializeVisualParams(const QString& encoded) {
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8());
    const QJsonObject root = doc.object();
    VisualParams params;
    params.heightStep = static_cast<float>(root["heightStep"].toDouble());
    params.outlineBias = static_cast<float>(root["outlineBias"].toDouble());
    params.smoothOneStep = root["smoothOneStep"].toBool(true);
    return params;
}

HexSphereModel buildModelFromSnapshot(const TerrainSnapshot& snapshot) {
    IcosphereBuilder builder;
    HexSphereModel model;
    model.rebuildFromIcosphere(builder.build(snapshot.subdivisionLevel));

    auto& cells = model.cells();
    const size_t count = std::min(snapshot.cells.size(), cells.size());
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

    return model;
}

QString serializeFloatArray(const std::vector<float>& values) {
    QJsonArray array;
    for (float value : values) {
        array.push_back(value);
    }
    return compactJson(array);
}

std::vector<float> deserializeFloatArray(const QString& encoded) {
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8());
    std::vector<float> result;
    if (!doc.isArray()) {
        return result;
    }
    const QJsonArray array = doc.array();
    result.reserve(static_cast<size_t>(array.size()));
    for (const auto& value : array) {
        result.push_back(static_cast<float>(value.toDouble()));
    }
    return result;
}

std::vector<float> buildSelectionOutline(
    const TerrainSnapshot& snapshot,
    const std::vector<int>& selectedCells,
    const VisualParams& visual) {
    HexSphereModel model = buildModelFromSnapshot(snapshot);
    QSet<int> selected;
    for (int cell : selectedCells) {
        selected.insert(cell);
    }
    return SelectionOutlineGenerator::buildSelectionOutlineVertices(
        model,
        selected,
        visual.heightStep,
        visual.outlineBias,
        visual.smoothOneStep);
}

TreeType chooseTreeType(Biome biome, std::mt19937& gen) {
    if (biome == Biome::Grass) {
        std::uniform_real_distribution<float> typeDist(0.0f, 1.0f);
        return typeDist(gen) < 0.3f ? TreeType::Fir : TreeType::Oak;
    }
    if (biome == Biome::Snow || biome == Biome::Tundra) {
        return TreeType::Fir;
    }
    return TreeType::Oak;
}

bool shouldPlaceTree(Biome biome, std::mt19937& gen) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    switch (biome) {
    case Biome::Grass:
        return dist(gen) < 0.28f;
    case Biome::Savanna:
        return dist(gen) < 0.16f;
    case Biome::Snow:
        return dist(gen) < 0.12f;
    case Biome::Tundra:
        return dist(gen) < 0.08f;
    default:
        return false;
    }
}

std::vector<TreePlacement> buildTreePlacements(const TerrainSnapshot& snapshot) {
    HexSphereModel model = buildModelFromSnapshot(snapshot);
    const auto& cells = model.cells();

    std::vector<TreePlacement> placements;
    placements.reserve(cells.size() / 6);

    const uint32_t baseSeed =
        snapshot.params.seed ^
        (static_cast<uint32_t>(snapshot.generatorIndex + 1) * 0x9e3779b9u) ^
        (static_cast<uint32_t>(snapshot.subdivisionLevel + 1) * 0x85ebca6bu);

    for (size_t i = 0; i < cells.size(); ++i) {
        const auto& cell = cells[i];
        std::mt19937 gen(baseSeed ^ (static_cast<uint32_t>(i + 1) * 0x27d4eb2du));
        if (!shouldPlaceTree(cell.biome, gen)) {
            continue;
        }

        std::uniform_real_distribution<float> baryDist(0.1f, 0.8f);
        std::uniform_real_distribution<float> scaleDist(0.7f, 1.3f);
        std::uniform_real_distribution<float> rotationDist(0.0f, 6.28318f);

        TreePlacement placement;
        placement.cellId = static_cast<int>(i);
        placement.treeType = chooseTreeType(cell.biome, gen);
        placement.triangleIdx = cell.poly.empty()
            ? 0
            : std::uniform_int_distribution<int>(0, static_cast<int>(cell.poly.size()) - 1)(gen);

        placement.baryU = baryDist(gen);
        placement.baryV = baryDist(gen);
        if (placement.baryU + placement.baryV > 1.0f) {
            placement.baryU = 1.0f - placement.baryU;
            placement.baryV = 1.0f - placement.baryV;
        }
        placement.baryW = 1.0f - placement.baryU - placement.baryV;
        placement.scale = scaleDist(gen);
        placement.rotation = rotationDist(gen);

        if (cell.biome == Biome::Savanna) {
            placement.colorType = TreePlacement::TreeColorType::Autumn;
            placement.isYellowCellTree = true;
            placement.foliageColor = QVector3D(0.86f, 0.55f, 0.14f);
            placement.trunkColor = QVector3D(0.35f, 0.20f, 0.08f);
            placement.scale *= 0.85f;
        }
        else if (placement.treeType == TreeType::Fir) {
            placement.foliageColor = QVector3D(0.16f, 0.45f, 0.30f);
            placement.trunkColor = QVector3D(0.38f, 0.24f, 0.12f);
            placement.scale *= 0.9f;
        }
        else {
            placement.foliageColor = QVector3D(0.24f, 0.68f, 0.18f);
            placement.trunkColor = QVector3D(0.50f, 0.34f, 0.16f);
            if (cell.humidity > 0.7f) {
                placement.scale *= 1.2f;
            }
            else if (cell.humidity < 0.3f) {
                placement.scale *= 0.7f;
            }
        }

        placements.push_back(placement);
    }

    return placements;
}

QJsonObject serializeTreePlacement(const TreePlacement& placement) {
    QJsonObject root;
    root["cellId"] = placement.cellId;
    root["triangleIdx"] = placement.triangleIdx;
    root["baryU"] = placement.baryU;
    root["baryV"] = placement.baryV;
    root["baryW"] = placement.baryW;
    root["scale"] = placement.scale;
    root["rotation"] = placement.rotation;
    root["treeType"] = static_cast<int>(placement.treeType);
    root["colorType"] = static_cast<int>(placement.colorType);
    root["foliageColor"] = serializeVec3(placement.foliageColor);
    root["trunkColor"] = serializeVec3(placement.trunkColor);
    root["isYellowCellTree"] = placement.isYellowCellTree;
    root["placementMode"] = static_cast<int>(placement.placementMode);
    root["worldPosition"] = serializeVec3(placement.worldPosition);
    root["worldUp"] = serializeVec3(placement.worldUp);
    root["worldYaw"] = placement.worldYaw;
    root["worldScale"] = placement.worldScale;
    return root;
}

TreePlacement deserializeTreePlacement(const QJsonObject& root) {
    TreePlacement placement;
    placement.cellId = root["cellId"].toInt(-1);
    placement.triangleIdx = root["triangleIdx"].toInt();
    placement.baryU = static_cast<float>(root["baryU"].toDouble(0.33));
    placement.baryV = static_cast<float>(root["baryV"].toDouble(0.33));
    placement.baryW = static_cast<float>(root["baryW"].toDouble(0.34));
    placement.scale = static_cast<float>(root["scale"].toDouble(1.0));
    placement.rotation = static_cast<float>(root["rotation"].toDouble());
    placement.treeType = static_cast<TreeType>(root["treeType"].toInt());
    placement.colorType = static_cast<TreePlacement::TreeColorType>(root["colorType"].toInt());
    placement.foliageColor = deserializeVec3(root["foliageColor"]);
    placement.trunkColor = deserializeVec3(root["trunkColor"]);
    placement.isYellowCellTree = root["isYellowCellTree"].toBool();
    placement.placementMode = static_cast<TreePlacement::PlacementMode>(root["placementMode"].toInt());
    placement.worldPosition = deserializeVec3(root["worldPosition"]);
    placement.worldUp = deserializeVec3(root["worldUp"]);
    placement.worldYaw = static_cast<float>(root["worldYaw"].toDouble());
    placement.worldScale = static_cast<float>(root["worldScale"].toDouble(1.0));
    return placement;
}

QString serializeTreePlacements(const std::vector<TreePlacement>& placements) {
    QJsonArray array;
    for (const auto& placement : placements) {
        array.push_back(serializeTreePlacement(placement));
    }
    return compactJson(array);
}

std::vector<TreePlacement> deserializeTreePlacements(const QString& encoded) {
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8());
    std::vector<TreePlacement> result;
    if (!doc.isArray()) {
        return result;
    }
    const QJsonArray array = doc.array();
    result.reserve(static_cast<size_t>(array.size()));
    for (const auto& value : array) {
        result.push_back(deserializeTreePlacement(value.toObject()));
    }
    return result;
}

QString serializeModelRequests(const std::vector<ModelPlacementRequest>& requests) {
    QJsonArray array;
    for (const auto& request : requests) {
        QJsonObject root;
        root["entityId"] = request.entityId;
        root["meshId"] = request.meshId;
        root["cellId"] = request.cellId;
        root["selected"] = request.selected;
        root["surfaceOffset"] = request.surfaceOffset;
        array.push_back(root);
    }
    return compactJson(array);
}

std::vector<ModelPlacementRequest> deserializeModelRequests(const QString& encoded) {
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8());
    std::vector<ModelPlacementRequest> result;
    if (!doc.isArray()) {
        return result;
    }
    const QJsonArray array = doc.array();
    result.reserve(static_cast<size_t>(array.size()));
    for (const auto& value : array) {
        const QJsonObject root = value.toObject();
        ModelPlacementRequest request;
        request.entityId = root["entityId"].toInt(-1);
        request.meshId = root["meshId"].toString();
        request.cellId = root["cellId"].toInt(-1);
        request.selected = root["selected"].toBool();
        request.surfaceOffset = static_cast<float>(root["surfaceOffset"].toDouble());
        result.push_back(request);
    }
    return result;
}

std::vector<ModelPlacement> buildModelPlacements(
    const TerrainSnapshot& snapshot,
    const std::vector<ModelPlacementRequest>& requests,
    const VisualParams& visual) {
    HexSphereModel model = buildModelFromSnapshot(snapshot);
    const auto& cells = model.cells();

    std::vector<ModelPlacement> result;
    result.reserve(requests.size());
    for (const auto& request : requests) {
        ModelPlacement placement;
        placement.entityId = request.entityId;
        placement.meshId = request.meshId;
        placement.cellId = request.cellId;
        placement.selected = request.selected;

        if (request.cellId >= 0 && request.cellId < static_cast<int>(cells.size())) {
            const Cell& cell = cells[static_cast<size_t>(request.cellId)];
            const float radius = 1.0f + static_cast<float>(cell.height) * visual.heightStep + request.surfaceOffset;
            placement.position = cell.centroid.normalized() * radius;
            placement.up = placement.position.normalized();
            placement.valid = true;
        }

        result.push_back(placement);
    }
    return result;
}

QString serializeModelPlacements(const std::vector<ModelPlacement>& placements) {
    QJsonArray array;
    for (const auto& placement : placements) {
        QJsonObject root;
        root["entityId"] = placement.entityId;
        root["meshId"] = placement.meshId;
        root["cellId"] = placement.cellId;
        root["selected"] = placement.selected;
        root["valid"] = placement.valid;
        root["position"] = serializeVec3(placement.position);
        root["up"] = serializeVec3(placement.up);
        array.push_back(root);
    }
    return compactJson(array);
}

std::vector<ModelPlacement> deserializeModelPlacements(const QString& encoded) {
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8());
    std::vector<ModelPlacement> result;
    if (!doc.isArray()) {
        return result;
    }
    const QJsonArray array = doc.array();
    result.reserve(static_cast<size_t>(array.size()));
    for (const auto& value : array) {
        const QJsonObject root = value.toObject();
        ModelPlacement placement;
        placement.entityId = root["entityId"].toInt(-1);
        placement.meshId = root["meshId"].toString();
        placement.cellId = root["cellId"].toInt(-1);
        placement.selected = root["selected"].toBool();
        placement.valid = root["valid"].toBool();
        placement.position = deserializeVec3(root["position"]);
        placement.up = deserializeVec3(root["up"]);
        result.push_back(placement);
    }
    return result;
}

proc::OperationRegistry makeSceneOperationRegistry() {
    proc::OperationRegistry registry = proc::make_builtin_operation_registry();
    registry.register_op("buildSelectionOutline", proc::v2::OpId{ 200 });
    registry.register_op("buildTreePlacements", proc::v2::OpId{ 201 });
    registry.register_op("buildModelPlacements", proc::v2::OpId{ 202 });
    return registry;
}

proc::GraphSchema buildSceneSchema() {
    proc::GraphSchema::StorageLayout roles;
    roles.inputs.insert("terrainSnapshot");
    roles.inputs.insert("selectedCells");
    roles.inputs.insert("visualParams");
    roles.inputs.insert("modelRequests");
    roles.inputs.insert("selectionDirty");
    roles.inputs.insert("treeDirty");
    roles.inputs.insert("modelDirty");
    roles.outputs.insert("selectionOutline");
    roles.outputs.insert("treePlacements");
    roles.outputs.insert("modelPlacements");
    roles.outputs.insert("selectionCacheHit");
    roles.outputs.insert("treeCacheHit");
    roles.outputs.insert("modelCacheHit");

    return proc::GraphSchemaBuilder::compile(
        roles,
        {
            {"terrainSnapshot", "str"},
            {"selectedCells", "str"},
            {"visualParams", "str"},
            {"modelRequests", "str"},
            {"selectionDirty", "int"},
            {"treeDirty", "int"},
            {"modelDirty", "int"},
            {"selectionOutline", "str"},
            {"treePlacements", "str"},
            {"modelPlacements", "str"},
            {"selectionCacheHit", "int"},
            {"treeCacheHit", "int"},
            {"modelCacheHit", "int"},
        },
        {
            proc::GraphSchemaBuilder::NodeDef{
                "BuildSelectionOutline",
                "buildSelectionOutline",
                {"terrainSnapshot", "selectedCells", "visualParams", "selectionDirty"},
                {"selectionOutline", "selectionCacheHit"},
                proc::GraphSchemaBuilder::GuardDef{ "selectionDirty", "1" },
            },
            proc::GraphSchemaBuilder::NodeDef{
                "BuildTreePlacements",
                "buildTreePlacements",
                {"terrainSnapshot", "treeDirty"},
                {"treePlacements", "treeCacheHit"},
                proc::GraphSchemaBuilder::GuardDef{ "treeDirty", "1" },
            },
            proc::GraphSchemaBuilder::NodeDef{
                "BuildModelPlacements",
                "buildModelPlacements",
                {"terrainSnapshot", "visualParams", "modelRequests", "modelDirty"},
                {"modelPlacements", "modelCacheHit"},
                proc::GraphSchemaBuilder::GuardDef{ "modelDirty", "1" },
            },
        },
        makeSceneOperationRegistry(),
        proc::make_builtin_algebra_registry());
}

} // namespace

struct DagSceneBackend::Impl {
    proc::GraphSchema schema;
    proc::RuntimeOperationRegistry runtimeRegistry;
    proc::GuardRegistry guardRegistry;
    proc::DefaultDagEngine engine;

    proc::v2::FieldSlot terrainSnapshotSlot{};
    proc::v2::FieldSlot selectedCellsSlot{};
    proc::v2::FieldSlot visualParamsSlot{};
    proc::v2::FieldSlot modelRequestsSlot{};
    proc::v2::FieldSlot selectionDirtySlot{};
    proc::v2::FieldSlot treeDirtySlot{};
    proc::v2::FieldSlot modelDirtySlot{};

    std::vector<proc::Field> outputs = {
        "selectionOutline",
        "treePlacements",
        "modelPlacements",
        "selectionCacheHit",
        "treeCacheHit",
        "modelCacheHit",
    };

    std::unordered_map<std::string, std::string> selectionCache;
    std::unordered_map<std::string, std::string> treeCache;
    std::unordered_map<std::string, std::string> modelCache;
    std::string lastSelectionKey;
    std::string lastTreeKey;
    std::string lastModelKey;
    DagDebugStats lastStats;

    Impl()
        : schema(buildSceneSchema())
        , runtimeRegistry(buildRuntimeRegistry())
        , guardRegistry(proc::make_builtin_guard_registry())
        , engine(schema, runtimeRegistry, guardRegistry) {
        bindSlots();

        proc::ValueStore init;
        init["terrainSnapshot"] = proc::make_value(std::string());
        init["selectedCells"] = proc::make_value(std::string("[]"));
        init["visualParams"] = proc::make_value(std::string("{}"));
        init["modelRequests"] = proc::make_value(std::string("[]"));
        init["selectionDirty"] = proc::make_value(std::string("0"));
        init["treeDirty"] = proc::make_value(std::string("0"));
        init["modelDirty"] = proc::make_value(std::string("0"));
        engine.init(init);
    }

    void bindSlots() {
        auto bind = [&](const char* name) {
            auto slot = schema.find_field(name);
            if (!slot) {
                throw std::runtime_error(std::string("DagSceneBackend missing field: ") + name);
            }
            return *slot;
            };

        terrainSnapshotSlot = bind("terrainSnapshot");
        selectedCellsSlot = bind("selectedCells");
        visualParamsSlot = bind("visualParams");
        modelRequestsSlot = bind("modelRequests");
        selectionDirtySlot = bind("selectionDirty");
        treeDirtySlot = bind("treeDirty");
        modelDirtySlot = bind("modelDirty");
    }

    proc::RuntimeOperationRegistry buildRuntimeRegistry() {
        proc::RuntimeOperationRegistry registry(makeSceneOperationRegistry());

        const auto selectionNode = schema.find_node("BuildSelectionOutline");
        const auto treeNode = schema.find_node("BuildTreePlacements");
        const auto modelNode = schema.find_node("BuildModelPlacements");
        const auto selectionOutlineSlot = schema.find_field("selectionOutline");
        const auto treePlacementsSlot = schema.find_field("treePlacements");
        const auto modelPlacementsSlot = schema.find_field("modelPlacements");
        const auto selectionCacheHitSlot = schema.find_field("selectionCacheHit");
        const auto treeCacheHitSlot = schema.find_field("treeCacheHit");
        const auto modelCacheHitSlot = schema.find_field("modelCacheHit");
        const auto terrainSlot = schema.find_field("terrainSnapshot");
        const auto selectedSlot = schema.find_field("selectedCells");
        const auto visualSlot = schema.find_field("visualParams");
        const auto modelRequestsSlotLocal = schema.find_field("modelRequests");
        const auto selectionDirtySlotLocal = schema.find_field("selectionDirty");
        const auto treeDirtySlotLocal = schema.find_field("treeDirty");
        const auto modelDirtySlotLocal = schema.find_field("modelDirty");

        if (!selectionNode || !treeNode || !modelNode ||
            !selectionOutlineSlot || !treePlacementsSlot || !modelPlacementsSlot ||
            !selectionCacheHitSlot || !treeCacheHitSlot || !modelCacheHitSlot ||
            !terrainSlot || !selectedSlot || !visualSlot || !modelRequestsSlotLocal ||
            !selectionDirtySlotLocal || !treeDirtySlotLocal || !modelDirtySlotLocal) {
            throw std::runtime_error("DagSceneBackend failed to bind schema");
        }

        registry.bind_executor(
            schema.op_of(*selectionNode),
            *selectionNode,
            [this,
             terrainSlot = *terrainSlot,
             selectedSlot = *selectedSlot,
             visualSlot = *visualSlot,
             outputSlot = *selectionOutlineSlot,
             cacheHitSlot = *selectionCacheHitSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn&,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                ++lastStats.executedNodes;
                const std::string terrainJson = readStringField(readHandle, terrainSlot);
                const std::string selectedJson = readStringField(readHandle, selectedSlot);
                const std::string visualJson = readStringField(readHandle, visualSlot);
                const std::string key = terrainJson + "|" + selectedJson + "|" + visualJson;

                bool cacheHit = false;
                std::string encoded;
                if (auto it = selectionCache.find(key); it != selectionCache.end()) {
                    encoded = it->second;
                    cacheHit = true;
                }
                else {
                    const auto snapshot = deserializeTerrainSnapshot(QString::fromUtf8(terrainJson.data(), static_cast<int>(terrainJson.size())));
                    if (snapshot) {
                        const auto outline = buildSelectionOutline(
                            *snapshot,
                            deserializeSelectedCells(QString::fromUtf8(selectedJson.data(), static_cast<int>(selectedJson.size()))),
                            deserializeVisualParams(QString::fromUtf8(visualJson.data(), static_cast<int>(visualJson.size()))));
                        encoded = serializeFloatArray(outline).toStdString();
                        selectionCache.emplace(key, encoded);
                    }
                }

                cacheHit ? ++lastStats.cacheHits : ++lastStats.cacheMisses;
                proc::Commit commit;
                commit.set(outputSlot, encoded, proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(outputSlot)));
                commit.set(cacheHitSlot, cacheHit ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(cacheHitSlot)));
                return commit;
            });

        registry.bind_executor(
            schema.op_of(*treeNode),
            *treeNode,
            [this,
             terrainSlot = *terrainSlot,
             outputSlot = *treePlacementsSlot,
             cacheHitSlot = *treeCacheHitSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn&,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                ++lastStats.executedNodes;
                const std::string terrainJson = readStringField(readHandle, terrainSlot);
                const std::string key = terrainJson;

                bool cacheHit = false;
                std::string encoded;
                if (auto it = treeCache.find(key); it != treeCache.end()) {
                    encoded = it->second;
                    cacheHit = true;
                }
                else {
                    const auto snapshot = deserializeTerrainSnapshot(QString::fromUtf8(terrainJson.data(), static_cast<int>(terrainJson.size())));
                    if (snapshot) {
                        encoded = serializeTreePlacements(buildTreePlacements(*snapshot)).toStdString();
                        treeCache.emplace(key, encoded);
                    }
                }

                cacheHit ? ++lastStats.cacheHits : ++lastStats.cacheMisses;
                proc::Commit commit;
                commit.set(outputSlot, encoded, proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(outputSlot)));
                commit.set(cacheHitSlot, cacheHit ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(cacheHitSlot)));
                return commit;
            });

        registry.bind_executor(
            schema.op_of(*modelNode),
            *modelNode,
            [this,
             terrainSlot = *terrainSlot,
             visualSlot = *visualSlot,
             modelRequestsSlotLocal = *modelRequestsSlotLocal,
             outputSlot = *modelPlacementsSlot,
             cacheHitSlot = *modelCacheHitSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn&,
                const proc::RuntimeOperationRegistry::DebugStringFn&) -> proc::Commit {
                ++lastStats.executedNodes;
                const std::string terrainJson = readStringField(readHandle, terrainSlot);
                const std::string visualJson = readStringField(readHandle, visualSlot);
                const std::string requestsJson = readStringField(readHandle, modelRequestsSlotLocal);
                const std::string key = terrainJson + "|" + visualJson + "|" + requestsJson;

                bool cacheHit = false;
                std::string encoded;
                if (auto it = modelCache.find(key); it != modelCache.end()) {
                    encoded = it->second;
                    cacheHit = true;
                }
                else {
                    const auto snapshot = deserializeTerrainSnapshot(QString::fromUtf8(terrainJson.data(), static_cast<int>(terrainJson.size())));
                    if (snapshot) {
                        encoded = serializeModelPlacements(buildModelPlacements(
                            *snapshot,
                            deserializeModelRequests(QString::fromUtf8(requestsJson.data(), static_cast<int>(requestsJson.size()))),
                            deserializeVisualParams(QString::fromUtf8(visualJson.data(), static_cast<int>(visualJson.size()))))).toStdString();
                        modelCache.emplace(key, encoded);
                    }
                }

                cacheHit ? ++lastStats.cacheHits : ++lastStats.cacheMisses;
                proc::Commit commit;
                commit.set(outputSlot, encoded, proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(outputSlot)));
                commit.set(cacheHitSlot, cacheHit ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(cacheHitSlot)));
                return commit;
            });

        return registry;
    }

    SceneDagResult rebuild(const SceneDagRequest& request) {
        const QString terrainJson = serializeTerrainSnapshot(request.terrain);
        const QString selectedJson = serializeSelectedCells(request.selectedCells);
        const QString visualJson = serializeVisualParams(VisualParams{
            request.heightStep,
            request.outlineBias,
            request.smoothOneStep,
        });
        const QString modelRequestsJson = serializeModelRequests(request.modelRequests);

        const std::string selectionKey = terrainJson.toStdString() + "|" + selectedJson.toStdString() + "|" + visualJson.toStdString();
        const std::string treeKey = terrainJson.toStdString();
        const std::string modelKey = terrainJson.toStdString() + "|" + visualJson.toStdString() + "|" + modelRequestsJson.toStdString();

        const bool selectionDirty = selectionKey != lastSelectionKey;
        const bool treeDirty = treeKey != lastTreeKey;
        const bool modelDirty = modelKey != lastModelKey;
        lastSelectionKey = selectionKey;
        lastTreeKey = treeKey;
        lastModelKey = modelKey;

        lastStats = {};
        lastStats.skippedGuardNodes =
            (selectionDirty ? 0 : 1) +
            (treeDirty ? 0 : 1) +
            (modelDirty ? 0 : 1);

        proc::Commit commit;
        commit.set(terrainSnapshotSlot, terrainJson.toStdString(), proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(terrainSnapshotSlot)));
        commit.set(selectedCellsSlot, selectedJson.toStdString(), proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(selectedCellsSlot)));
        commit.set(visualParamsSlot, visualJson.toStdString(), proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(visualParamsSlot)));
        commit.set(modelRequestsSlot, modelRequestsJson.toStdString(), proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(modelRequestsSlot)));
        commit.set(selectionDirtySlot, selectionDirty ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(selectionDirtySlot)));
        commit.set(treeDirtySlot, treeDirty ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(treeDirtySlot)));
        commit.set(modelDirtySlot, modelDirty ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(modelDirtySlot)));
        engine.push_input(commit);

        try {
            engine.flush_prepare(outputs);
        }
        catch (const std::exception& e) {
            qWarning() << "DagSceneBackend::flush_prepare failed:" << e.what();
            return {};
        }

        const auto& prepared = engine.prepared_output_store();
        SceneDagResult result;
        if (auto value = proc::get_value_view(prepared, "selectionOutline")) {
            result.selectionOutline.vertices = deserializeFloatArray(QString::fromUtf8(value->data(), static_cast<int>(value->size())));
        }
        if (auto value = proc::get_value_view(prepared, "treePlacements")) {
            result.treePlacements = deserializeTreePlacements(QString::fromUtf8(value->data(), static_cast<int>(value->size())));
        }
        if (auto value = proc::get_value_view(prepared, "modelPlacements")) {
            result.modelPlacements = deserializeModelPlacements(QString::fromUtf8(value->data(), static_cast<int>(value->size())));
        }

        engine.ack_outputs();
        return result;
    }
};

DagSceneBackend::DagSceneBackend()
    : impl_(std::make_unique<Impl>()) {
}

DagSceneBackend::~DagSceneBackend() = default;
DagSceneBackend::DagSceneBackend(DagSceneBackend&&) noexcept = default;
DagSceneBackend& DagSceneBackend::operator=(DagSceneBackend&&) noexcept = default;

SceneDagResult DagSceneBackend::rebuild(const SceneDagRequest& request) {
    return impl_->rebuild(request);
}

const DagDebugStats& DagSceneBackend::lastStats() const {
    return impl_->lastStats;
}
