#include "DagPathBackend.h"

#include <QtDebug>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

#include "model/HexSphereModel.h"
#include "controllers/PathBuilder.h"
#include "InProcessDagPayloads.h"
#include "TerrainBackendTypes.h"

#include <proc/ProcessDag.h>
#include <proc/Schema.h>
#include <proc/Logging.h>

namespace {
    struct PathPayloads {
        InProcessDagPayloads<TerrainSnapshot> terrain{"path-terrain"};
        InProcessDagPayloads<PathResult> results{"path-result"};
        // Legacy fallback only: snapshots without a shared topology.
        std::shared_ptr<const HexSphereModel> model;
        // Height/biome view over the active snapshot, rebuilt once per token.
        std::shared_ptr<const std::vector<TerrainCellState>> state;
        std::string activeTerrainToken;
    };


    // ============================================================
    // ПОСТРОЕНИЕ МОДЕЛИ ИЗ СНАПШОТА
    // ============================================================

    HexSphereModel buildModelFromSnapshot(const TerrainSnapshot& snapshot) {
        if (!snapshot.topology || snapshot.topology->subdivisionLevel != snapshot.subdivisionLevel) {
            throw std::runtime_error("TerrainSnapshot is missing a matching immutable topology");
        }
        HexSphereModel model(snapshot.topology);

        // Получаем НЕ-const ссылку на ячейки (см. HexSphereModel.h)
        auto& cells = model.cells();

        // Заполняем ячейки данными из снапшота
        const size_t count = std::min(snapshot.cells.size(), cells.size());
        for (size_t i = 0; i < count; ++i) {
            const auto& src = snapshot.cells[i];
            auto& dst = cells[i];

            dst.height = src.height;
            dst.biome = src.biome;
            dst.temperature = src.temperature;
            dst.humidity = src.humidity;
            dst.pressure = src.pressure;
            dst.oreDensity = src.oreDensity;
            dst.oreType = src.oreType;
            // Centroids are supplied by the shared immutable topology.
        }

        return model;
    }

    // ============================================================
    // ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ЧТЕНИЯ ПОЛЕЙ DAG
    // ============================================================

    int readIntField(
        const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
        const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
        proc::v2::FieldSlot slot,
        int fallback) {

        const auto handle = readHandle(slot);
        const auto debugView = proc::Commit::debug_view(handle);
        const auto debugValue = QString::fromUtf8(
            debugView.data(), static_cast<qsizetype>(debugView.size()));

        bool ok = false;
        const int parsed = debugValue.toInt(&ok);
        if (!ok) {
            qWarning() << "DagPathBackend failed to parse int field"
                << fieldName(slot).data() << "from" << debugValue;
            return fallback;
        }
        return parsed;
    }

    std::string readStringField(
        const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
        proc::v2::FieldSlot slot) {

        const auto handle = readHandle(slot);
        const auto debugView = proc::Commit::debug_view(handle);
        return std::string(debugView.data(), debugView.size());
    }

    proc::Commit executeFindPath(
        const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
        const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
        const proc::RuntimeOperationRegistry::DebugStringFn&,
        proc::v2::FieldSlot terrainSnapshotSlot,
        proc::v2::FieldSlot smoothMaxDeltaSlot,
        proc::v2::FieldSlot startCellSlot,
        proc::v2::FieldSlot goalCellSlot,
        proc::v2::FieldSlot pathResultSlot,
        const proc::GraphSchema& schema,
        const std::shared_ptr<PathPayloads>& payloads) {

        const auto snapshot = payloads->terrain.resolve(readStringField(readHandle, terrainSnapshotSlot));

        const int smoothMaxDelta = readIntField(readHandle, fieldName, smoothMaxDeltaSlot, 1);
        const int startId = readIntField(readHandle, fieldName, startCellSlot, 0);
        const int goalId = readIntField(readHandle, fieldName, goalCellSlot, 0);

        // Строим граф и ищем путь.
        // Деревья и реки из снапшота — препятствия: дорога не проходит через
        // занятые ими клетки, и закончиться на такой клетке она тоже не может.
        std::unique_ptr<PathBuilder> builder;
        if (snapshot->topology && payloads->state) {
            builder = std::make_unique<PathBuilder>(
                *snapshot->topology, *payloads->state, smoothMaxDelta);
        }
        else if (payloads->model) {
            builder = std::make_unique<PathBuilder>(*payloads->model, smoothMaxDelta);
        }
        else {
            return {};
        }
        std::vector<int> blockedCells = snapshot->treeCells;
        blockedCells.insert(blockedCells.end(),
            snapshot->riverCells.begin(), snapshot->riverCells.end());
        builder->setBlockedCells(blockedCells);
        builder->build();
        std::vector<int> path = builder->astar(startId, goalId);

        PathResult result;
        result.found = !path.empty();
        result.cellIds = std::move(path);
        if (snapshot->topology) {
            const auto& topologyCells = snapshot->topology->cells;
            for (size_t i = 1; i < result.cellIds.size(); ++i) {
                const QVector3D& a = topologyCells[static_cast<size_t>(result.cellIds[i - 1])].centroid;
                const QVector3D& b = topologyCells[static_cast<size_t>(result.cellIds[i])].centroid;
                const float dot = std::clamp(
                    QVector3D::dotProduct(a.normalized(), b.normalized()), -1.0f, 1.0f);
                result.length += std::acos(dot);
            }
        }
        else if (payloads->model) {
            const auto& cells = payloads->model->cells();
            for (size_t i = 1; i < result.cellIds.size(); ++i) {
                result.length += PathBuilder::edgeAngularDistance(
                    cells[result.cellIds[i - 1]], cells[result.cellIds[i]]);
            }
        }

        proc::Commit commit;
        commit.set(
            pathResultSlot,
            payloads->results.put(std::move(result)),
            proc::v2::WriteLifetime::Persistent,
            std::string(schema.field_name(pathResultSlot)));

        return commit;
    }

    proc::OperationRegistry makePathOperationRegistry() {
        proc::OperationRegistry registry = proc::make_builtin_operation_registry();
        registry.register_op("findPath", proc::v2::OpId{ 100 });

        return registry;
    }

    proc::AlgebraRegistry makePathAlgebraRegistry() {
        return proc::make_builtin_algebra_registry();
    }

    // ============================================================
    // ПОСТРОЕНИЕ СХЕМЫ
    // ============================================================

    proc::GraphSchema buildPathSchema() {
        proc::GraphSchema::StorageLayout roles;

        roles.inputs.insert("terrainSnapshot");
        roles.inputs.insert("smoothMaxDelta");
        roles.inputs.insert("startCellId");
        roles.inputs.insert("goalCellId");

        roles.outputs.insert("pathResult");

        return proc::GraphSchemaBuilder::compile(
            roles,
            {
                {"terrainSnapshot", "str"},
                {"smoothMaxDelta", "int"},
                {"startCellId", "int"},
                {"goalCellId", "int"},
                {"pathResult", "str"},
            },
        {
            proc::GraphSchemaBuilder::NodeDef{
                "FindPath",
                "findPath",
                {"terrainSnapshot", "smoothMaxDelta", "startCellId", "goalCellId"},
                {"pathResult"},
                std::nullopt,
            },
        },
        makePathOperationRegistry(),
        makePathAlgebraRegistry());
    }

    // ============================================================
    // ПОСТРОЕНИЕ РЕЕСТРА ИСПОЛНИТЕЛЕЙ
    // ============================================================

    proc::RuntimeOperationRegistry buildPathRuntimeRegistry(const proc::GraphSchema& schema, const std::shared_ptr<PathPayloads>& payloads) {
        proc::RuntimeOperationRegistry registry(makePathOperationRegistry());

        const auto findNodeSlot = schema.find_node("FindPath");
        const auto terrainSnapshotSlot = schema.find_field("terrainSnapshot");
        const auto smoothMaxDeltaSlot = schema.find_field("smoothMaxDelta");
        const auto startCellSlot = schema.find_field("startCellId");
        const auto goalCellSlot = schema.find_field("goalCellId");
        const auto pathResultSlot = schema.find_field("pathResult");

        if (!findNodeSlot ||
            !terrainSnapshotSlot || !smoothMaxDeltaSlot ||
            !startCellSlot || !goalCellSlot ||
            !pathResultSlot) {
            throw std::runtime_error("DagPathBackend: failed to bind schema slots");
        }

        registry.bind_executor(
            schema.op_of(*findNodeSlot),
            *findNodeSlot,
            [&schema, payloads,
            tsSlot = *terrainSnapshotSlot,
            smdSlot = *smoothMaxDeltaSlot,
            scSlot = *startCellSlot,
            gcSlot = *goalCellSlot,
            prSlot = *pathResultSlot](
                const proc::RuntimeOperationRegistry::ReadHandleFn& readHandle,
                const proc::RuntimeOperationRegistry::FieldNameFn& fieldName,
                const proc::RuntimeOperationRegistry::DebugStringFn& debugString) -> proc::Commit {

                    return executeFindPath(
                        readHandle, fieldName, debugString,
                        tsSlot, smdSlot, scSlot, gcSlot, prSlot, schema, payloads);
            });

        return registry;
    }

} // namespace

// ============================================================
// СТРУКТУРА Impl
// ============================================================

struct DagPathBackend::Impl {
    std::shared_ptr<PathPayloads> payloads = std::make_shared<PathPayloads>();
    int smoothMaxDelta = 1;
    PathResult lastResult;
    proc::GraphSchema schema;
    proc::RuntimeOperationRegistry runtimeRegistry;
    proc::GuardRegistry guardRegistry;
    proc::DefaultDagEngine engine;
    proc::v2::FieldSlot terrainSnapshotSlot;
    proc::v2::FieldSlot smoothMaxDeltaSlot;
    proc::v2::FieldSlot startCellIdSlot;
    proc::v2::FieldSlot goalCellIdSlot;
    std::vector<proc::Field> outputs = {
    "pathResult"
    };

    Impl()
        : schema(buildPathSchema())
        , runtimeRegistry(buildPathRuntimeRegistry(schema, payloads))
        , guardRegistry(proc::make_builtin_guard_registry())
        , engine(schema, runtimeRegistry, guardRegistry)
    {
        auto tsSlot = schema.find_field("terrainSnapshot");
        auto smdSlot = schema.find_field("smoothMaxDelta");
        auto scSlot = schema.find_field("startCellId");
        auto gcSlot = schema.find_field("goalCellId");

        if (!tsSlot || !smdSlot || !scSlot || !gcSlot) {
            throw std::runtime_error("DagPathBackend::Impl: failed to find field slots");
        }

        terrainSnapshotSlot = *tsSlot;
        smoothMaxDeltaSlot = *smdSlot;
        startCellIdSlot = *scSlot;
        goalCellIdSlot = *gcSlot;

        proc::ValueStore init;
        init["terrainSnapshot"] = proc::make_value(std::string(""));
        init["smoothMaxDelta"] = proc::make_value(std::to_string(1));
        init["startCellId"] = proc::make_value(std::to_string(0));
        init["goalCellId"] = proc::make_value(std::to_string(0));
        engine.init(init);
    }

    void pushTerrainSnapshot(const TerrainSnapshot& snapshot) {
        const std::string token = payloads->terrain.intern(snapshot, sameTerrainSnapshot);
        if (token != payloads->activeTerrainToken) {
            payloads->activeTerrainToken = token;
            payloads->model.reset();
            payloads->state.reset();
            if (snapshot.topology) {
                auto state = std::make_shared<std::vector<TerrainCellState>>(snapshot.cells.size());
                for (size_t i = 0; i < snapshot.cells.size(); ++i) {
                    (*state)[i].height = snapshot.cells[i].height;
                    (*state)[i].biome = snapshot.cells[i].biome;
                }
                payloads->state = std::move(state);
            }
            else {
                // Restored/legacy snapshot: materialize a model once per token.
                payloads->model = std::make_shared<const HexSphereModel>(
                    buildModelFromSnapshot(snapshot));
            }
        }
        proc::Commit c;
        c.set(
            terrainSnapshotSlot,                               // ← ИСПОЛЬЗУЕМ СЛОТ
            token,
            proc::v2::WriteLifetime::Persistent,
            std::string(schema.field_name(terrainSnapshotSlot)));
        engine.push_input(c);
    }

    void pushSmoothMaxDelta(int delta) {
        smoothMaxDelta = delta;
        proc::Commit c;
        c.set(
            smoothMaxDeltaSlot,                                // ← ИСПОЛЬЗУЕМ СЛОТ
            std::to_string(delta),
            proc::v2::WriteLifetime::Persistent,
            std::string(schema.field_name(smoothMaxDeltaSlot)));
        engine.push_input(c);
    }

    PathResult findPath(int startId, int goalId) {
        proc::Commit c;
        c.set(
            startCellIdSlot,
            std::to_string(startId),
            proc::v2::WriteLifetime::Persistent,
            std::string(schema.field_name(startCellIdSlot)));
        c.set(
            goalCellIdSlot,
            std::to_string(goalId),
            proc::v2::WriteLifetime::Persistent,
            std::string(schema.field_name(goalCellIdSlot)));
        engine.push_input(c);

        try {
            if (!engine.flush_prepare(outputs)) return {};
        }
        catch (const std::exception& e) {
            qWarning() << "DagPathBackend::flush_prepare failed:" << e.what();
            return PathResult{};
        }

        auto& prepared = engine.prepared_output_store();

        PathResult result;
        auto pathView = proc::get_value_view(prepared, "pathResult");
        if (!pathView) return {};
        result = *payloads->results.resolve(*pathView);
        if (!engine.ack_outputs()) return {};

        lastResult = result;
        return result;
    }
};

// ============================================================
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// ============================================================

DagPathBackend::DagPathBackend()
    : impl_(std::make_unique<Impl>()) {
}

DagPathBackend::~DagPathBackend() = default;
DagPathBackend::DagPathBackend(DagPathBackend&&) noexcept = default;
DagPathBackend& DagPathBackend::operator=(DagPathBackend&&) noexcept = default;

void DagPathBackend::setTerrainSnapshot(const TerrainSnapshot& snapshot) {
    impl_->pushTerrainSnapshot(snapshot);
}

void DagPathBackend::setSmoothMaxDelta(int delta) {
    impl_->pushSmoothMaxDelta(delta);
}

PathResult DagPathBackend::findPath(int startCellId, int goalCellId) {
    return impl_->findPath(startCellId, goalCellId);
}

const PathResult& DagPathBackend::lastResult() const {
    return impl_->lastResult;
}
