#include "DagSceneBackend.h"

#include <QSet>
#include <QtDebug>

#include <algorithm>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "InProcessDagPayloads.h"
#include "generation/MeshGenerators/SelectionOutlineGenerator.h"

#include <proc/ProcessDag.h>
#include <proc/Schema.h>
#include <proc/Logging.h>

namespace {

    std::string readStringField(
        const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
        proc::v2::FieldSlot slot) {
        const auto handle = readHandle(slot);
        const auto debugView = proc::Commit::debug_view(handle);
        return std::string(debugView.data(), debugView.size());
    }

    struct VisualParams {
        float heightStep = 0.0f;
        float outlineBias = 0.0f;
        bool smoothOneStep = true;
    };

    HexSphereModel buildModelFromSnapshot(const TerrainSnapshot& snapshot) {
        if (!snapshot.topology || snapshot.topology->subdivisionLevel != snapshot.subdivisionLevel) {
            throw std::runtime_error("TerrainSnapshot is missing a matching immutable topology");
        }
        HexSphereModel model(snapshot.topology);

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
        }

        return model;
    }

    std::vector<float> buildSelectionOutline(const HexSphereModel& model,
        const std::vector<int>& selectedCells, const VisualParams& visual) {
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

    std::vector<float> buildSelectionOutlineFromSnapshot(
        const TerrainSnapshot& snapshot,
        const std::vector<int>& selectedCells,
        const VisualParams& visual) {
        if (!snapshot.topology) return {};
        QSet<int> selected;
        for (int cell : selectedCells) selected.insert(cell);
        return SelectionOutlineGenerator::buildSelectionOutlineVertices(
            *snapshot.topology,
            [&snapshot](int cellId) {
                return cellId >= 0 && static_cast<size_t>(cellId) < snapshot.cells.size()
                    ? snapshot.cells[static_cast<size_t>(cellId)].height
                    : 0;
            },
            selected, visual.heightStep, visual.outlineBias, visual.smoothOneStep);
    }

    // ======================= TREE PLACEMENTS =======================

    bool shouldPlaceTree(Biome biome, std::mt19937& gen) {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        switch (biome) {
        case Biome::Grass:   return dist(gen) < 0.35f;
        case Biome::Savanna: return dist(gen) < 0.25f;
        case Biome::Jungle:  return dist(gen) < 0.45f;
        default:             return false;
        }
    }

    std::vector<TreePlacement> buildTreePlacements(const TerrainSnapshot& snapshot, const HexSphereModel& model) {
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

            // �� ������ �� ������ >= 2
            if (cell.height >= 2) {
                continue;
            }

            // �� ������ ����� � �����
            bool hasWaterNeighbor = false;
            for (int neighborId : cell.neighbors) {
                if (neighborId >= 0 && neighborId < static_cast<int>(cells.size())) {
                    if (cells[static_cast<size_t>(neighborId)].biome == Biome::Sea) {
                        hasWaterNeighbor = true;
                        break;
                    }
                }
            }
            if (hasWaterNeighbor) {
                continue;
            }

            std::uniform_real_distribution<float> baryDist(0.1f, 0.8f);
            std::uniform_real_distribution<float> scaleDist(0.7f, 1.3f);
            std::uniform_real_distribution<float> rotationDist(0.0f, 6.28318f);

            TreePlacement placement;
            placement.cellId = static_cast<int>(i);
            placement.triangleIdx = cell.poly.empty()
                ? 0
                : std::uniform_int_distribution<int>(
                    0, static_cast<int>(cell.poly.size()) - 1)(gen);

            placement.baryU = baryDist(gen);
            placement.baryV = baryDist(gen);
            if (placement.baryU + placement.baryV > 1.0f) {
                placement.baryU = 1.0f - placement.baryU;
                placement.baryV = 1.0f - placement.baryV;
            }
            placement.baryW = 1.0f - placement.baryU - placement.baryV;
            placement.rotation = rotationDist(gen);

            // �������: ������� �20%, + ��������� �� ���������
            float s = scaleDist(gen);
            if (cell.biome == Biome::Jungle && cell.humidity > 0.7f) {
                s *= 1.15f;
            }
            else if (cell.biome == Biome::Savanna && cell.humidity < 0.3f) {
                s *= 0.85f;
            }
            placement.scale = s;

            // ����� �� �������� � placement � �� ���� �������� �� TreeVariant.
            placements.push_back(placement);
        }

        return placements;
    }

    std::vector<TreePlacement> buildTreePlacementsFromSnapshot(
        const TerrainSnapshot& snapshot) {
        if (!snapshot.topology) {
            return {};
        }
        const auto& topologyCells = snapshot.topology->cells;
        const auto& cells = snapshot.cells;
        const size_t count = std::min(topologyCells.size(), cells.size());

        std::vector<TreePlacement> placements;
        placements.reserve(count / 6);

        const uint32_t baseSeed =
            snapshot.params.seed ^
            (static_cast<uint32_t>(snapshot.generatorIndex + 1) * 0x9e3779b9u) ^
            (static_cast<uint32_t>(snapshot.subdivisionLevel + 1) * 0x85ebca6bu);

        for (size_t i = 0; i < count; ++i) {
            const auto& cell = topologyCells[i];
            const auto& state = cells[i];
            std::mt19937 gen(baseSeed ^ (static_cast<uint32_t>(i + 1) * 0x27d4eb2du));

            if (!shouldPlaceTree(state.biome, gen)) {
                continue;
            }
            if (state.height >= 2) {
                continue;
            }

            bool hasWaterNeighbor = false;
            for (int neighborId : cell.neighbors) {
                if (neighborId >= 0 && static_cast<size_t>(neighborId) < count
                    && cells[static_cast<size_t>(neighborId)].biome == Biome::Sea) {
                    hasWaterNeighbor = true;
                    break;
                }
            }
            if (hasWaterNeighbor) {
                continue;
            }

            std::uniform_real_distribution<float> baryDist(0.1f, 0.8f);
            std::uniform_real_distribution<float> scaleDist(0.7f, 1.3f);
            std::uniform_real_distribution<float> rotationDist(0.0f, 6.28318f);

            TreePlacement placement;
            placement.cellId = static_cast<int>(i);
            placement.triangleIdx = cell.poly.empty()
                ? 0
                : std::uniform_int_distribution<int>(
                    0, static_cast<int>(cell.poly.size()) - 1)(gen);

            placement.baryU = baryDist(gen);
            placement.baryV = baryDist(gen);
            if (placement.baryU + placement.baryV > 1.0f) {
                placement.baryU = 1.0f - placement.baryU;
                placement.baryV = 1.0f - placement.baryV;
            }
            placement.baryW = 1.0f - placement.baryU - placement.baryV;
            placement.rotation = rotationDist(gen);

            float s = scaleDist(gen);
            if (state.biome == Biome::Jungle && state.humidity > 0.7f) {
                s *= 1.15f;
            }
            else if (state.biome == Biome::Savanna && state.humidity < 0.3f) {
                s *= 0.85f;
            }
            placement.scale = s;

            placements.push_back(placement);
        }

        return placements;
    }

    std::vector<ModelPlacement> buildModelPlacements(const HexSphereModel& model,
        const std::vector<ModelPlacementRequest>& requests, const VisualParams& visual) {
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

    std::vector<ModelPlacement> buildModelPlacementsFromSnapshot(
        const TerrainSnapshot& snapshot,
        const std::vector<ModelPlacementRequest>& requests,
        const VisualParams& visual) {
        std::vector<ModelPlacement> result;
        result.reserve(requests.size());
        if (!snapshot.topology) return result;
        for (const auto& request : requests) {
            ModelPlacement placement;
            placement.entityId = request.entityId;
            placement.meshId = request.meshId;
            placement.cellId = request.cellId;
            placement.selected = request.selected;
            if (request.cellId >= 0
                && static_cast<size_t>(request.cellId) < snapshot.topology->cells.size()
                && static_cast<size_t>(request.cellId) < snapshot.cells.size()) {
                const auto& cell = snapshot.topology->cells[static_cast<size_t>(request.cellId)];
                const float radius = 1.0f
                    + static_cast<float>(snapshot.cells[static_cast<size_t>(request.cellId)].height)
                        * visual.heightStep
                    + request.surfaceOffset;
                placement.position = cell.centroid.normalized() * radius;
                placement.up = placement.position.normalized();
                placement.valid = true;
            }
            result.push_back(std::move(placement));
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

    InProcessDagPayloads<TerrainSnapshot> terrainPayloads{"terrain"};
    InProcessDagPayloads<std::vector<int>> selectionInputs{"selected"};
    InProcessDagPayloads<VisualParams> visualInputs{"visual"};
    InProcessDagPayloads<std::vector<ModelPlacementRequest>> modelInputs{"models"};
    InProcessDagPayloads<std::vector<float>> selectionOutputs{"outline"};
    InProcessDagPayloads<std::vector<TreePlacement>> treeOutputs{"trees"};
    InProcessDagPayloads<std::vector<ModelPlacement>> modelOutputs{"placements"};
    InProcessDagCache<std::vector<float>> selectionCache;
    InProcessDagCache<std::vector<TreePlacement>> treeCache;
    InProcessDagCache<std::vector<ModelPlacement>> modelCache;
    std::string lastSelectionKey;
    std::string lastTreeKey;
    std::string lastModelKey;
    std::string activeTerrainToken;
    std::shared_ptr<const HexSphereModel> activeModel;
    DagDebugStats lastStats;

    Impl()
        : schema(buildSceneSchema())
        , runtimeRegistry(buildRuntimeRegistry())
        , guardRegistry(proc::make_builtin_guard_registry())
        , engine(schema, runtimeRegistry, guardRegistry) {
        bindSlots();

        proc::ValueStore init;
        init["terrainSnapshot"] = proc::make_value(std::string());
        init["selectedCells"] = proc::make_value(std::string());
        init["visualParams"] = proc::make_value(std::string());
        init["modelRequests"] = proc::make_value(std::string());
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
                    const std::string terrainToken = readStringField(readHandle, terrainSlot);
                    const std::string selectedToken = readStringField(readHandle, selectedSlot);
                    const std::string visualToken = readStringField(readHandle, visualSlot);
                    const std::string key = terrainToken + "|" + selectedToken + "|" + visualToken;

                    const auto [value, cacheHit] = selectionCache.get(key, [&] {
                        const auto snapshot = terrainPayloads.resolve(terrainToken);
                        const auto selected = selectionInputs.resolve(selectedToken);
                        const auto visual = visualInputs.resolve(visualToken);
                        if (snapshot && snapshot->topology) {
                            return buildSelectionOutlineFromSnapshot(*snapshot, *selected, *visual);
                        }
                        if (!activeModel) return std::vector<float>{};
                        return buildSelectionOutline(*activeModel, *selected, *visual);
                    });
                    const std::string encoded = selectionOutputs.put(value);

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
                    const std::string terrainToken = readStringField(readHandle, terrainSlot);
                    const std::string key = terrainToken;

                    const auto [value, cacheHit] = treeCache.get(key, [&] {
                        const auto snapshot = terrainPayloads.resolve(terrainToken);
                        if (snapshot && snapshot->topology) {
                            return buildTreePlacementsFromSnapshot(*snapshot);
                        }
                        if (!snapshot || !activeModel) return std::vector<TreePlacement>{};
                        return buildTreePlacements(*snapshot, *activeModel);
                    });
                    const std::string encoded = treeOutputs.put(value);

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
                    const std::string terrainToken = readStringField(readHandle, terrainSlot);
                    const std::string visualToken = readStringField(readHandle, visualSlot);
                    const std::string requestsToken = readStringField(readHandle, modelRequestsSlotLocal);
                    const std::string key = terrainToken + "|" + visualToken + "|" + requestsToken;

                    const auto [value, cacheHit] = modelCache.get(key, [&] {
                        const auto snapshot = terrainPayloads.resolve(terrainToken);
                        const auto requests = modelInputs.resolve(requestsToken);
                        const auto visual = visualInputs.resolve(visualToken);
                        if (snapshot && snapshot->topology) {
                            return buildModelPlacementsFromSnapshot(*snapshot, *requests, *visual);
                        }
                        if (!activeModel) return std::vector<ModelPlacement>{};
                        return buildModelPlacements(*activeModel, *requests, *visual);
                    });
                    const std::string encoded = modelOutputs.put(value);

                    cacheHit ? ++lastStats.cacheHits : ++lastStats.cacheMisses;
                    proc::Commit commit;
                    commit.set(outputSlot, encoded, proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(outputSlot)));
                    commit.set(cacheHitSlot, cacheHit ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(cacheHitSlot)));
                    return commit;
            });

        return registry;
    }

    SceneDagResult rebuild(const SceneDagRequest& request) {
        const std::size_t planCacheHitsBefore = engine.plan_cache_hits();
        const std::size_t planCacheMissesBefore = engine.plan_cache_misses();
        const std::string terrainToken = terrainPayloads.intern(request.terrain, sameTerrainSnapshot);
        if (terrainToken != activeTerrainToken) {
            activeTerrainToken = terrainToken;
            activeModel.reset();
            // Snapshots with shared topology drive every node directly; only
            // restored/legacy snapshots need the materialized model fallback.
            if (!request.terrain.topology) {
                activeModel = std::make_shared<const HexSphereModel>(
                    buildModelFromSnapshot(request.terrain));
            }
        }
        const std::string selectedToken = selectionInputs.intern(request.selectedCells,
            [](const auto& a, const auto& b) { return a == b; });
        const std::string visualToken = visualInputs.intern(
            VisualParams{request.heightStep, request.outlineBias, request.smoothOneStep},
            [](const auto& a, const auto& b) {
                return a.heightStep == b.heightStep && a.outlineBias == b.outlineBias
                    && a.smoothOneStep == b.smoothOneStep;
            });
        const std::string modelRequestsToken = modelInputs.intern(request.modelRequests,
            [](const auto& a, const auto& b) {
                return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                    [](const auto& x, const auto& y) {
                        return x.entityId == y.entityId && x.meshId == y.meshId && x.cellId == y.cellId
                            && x.selected == y.selected && x.surfaceOffset == y.surfaceOffset;
                    });
            });
        const std::string selectionKey = terrainToken + "|" + selectedToken + "|" + visualToken;
        const std::string treeKey = terrainToken;
        const std::string modelKey = terrainToken + "|" + visualToken + "|" + modelRequestsToken;

        const bool selectionDirty = selectionKey != lastSelectionKey;
        const bool treeDirty = treeKey != lastTreeKey;
        const bool modelDirty = modelKey != lastModelKey;

        lastStats = {};
        lastStats.skippedGuardNodes =
            (selectionDirty ? 0 : 1) +
            (treeDirty ? 0 : 1) +
            (modelDirty ? 0 : 1);

        proc::Commit commit;
        commit.set(terrainSnapshotSlot, terrainToken, proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(terrainSnapshotSlot)));
        commit.set(selectedCellsSlot, selectedToken, proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(selectedCellsSlot)));
        commit.set(visualParamsSlot, visualToken, proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(visualParamsSlot)));
        commit.set(modelRequestsSlot, modelRequestsToken, proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(modelRequestsSlot)));
        commit.set(selectionDirtySlot, selectionDirty ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(selectionDirtySlot)));
        commit.set(treeDirtySlot, treeDirty ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(treeDirtySlot)));
        commit.set(modelDirtySlot, modelDirty ? "1" : "0", proc::v2::WriteLifetime::Persistent, std::string(schema.field_name(modelDirtySlot)));
        engine.push_input(commit);

        try {
            if (!engine.flush_prepare(outputs)) return {};
        }
        catch (const std::exception& e) {
            qWarning() << "DagSceneBackend::flush_prepare failed:" << e.what();
            return {};
        }

        const auto& prepared = engine.prepared_output_store();
        SceneDagResult result;
        if (auto value = proc::get_value_view(prepared, "selectionOutline")) {
            result.selectionOutline.vertices = *selectionOutputs.resolve(*value);
        }
        if (auto value = proc::get_value_view(prepared, "treePlacements")) {
            result.treePlacements = *treeOutputs.resolve(*value);
        }
        if (auto value = proc::get_value_view(prepared, "modelPlacements")) {
            result.modelPlacements = *modelOutputs.resolve(*value);
        }

        lastStats.planCacheHits = static_cast<int>(engine.plan_cache_hits() - planCacheHitsBefore);
        lastStats.planCacheMisses = static_cast<int>(engine.plan_cache_misses() - planCacheMissesBefore);

        if (!engine.ack_outputs()) return {};
        lastSelectionKey = selectionKey;
        lastTreeKey = treeKey;
        lastModelKey = modelKey;
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
