#include "DagPathBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtDebug>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include "model/HexSphereModel.h"
#include "controllers/PathBuilder.h"
#include "TerrainSerialization.h"
#include "TerrainBackendTypes.h"

#include <proc/ProcessDag.h>
#include <proc/Schema.h>
#include <proc/Logging.h>

namespace {

    // ============================================================
    // ПОСТРОЕНИЕ МОДЕЛИ ИЗ СНАПШОТА
    // ============================================================

    HexSphereModel buildModelFromSnapshot(const TerrainSnapshot& snapshot) {
        IcosphereBuilder builder;
        HexSphereModel model;
        model.rebuildFromIcosphere(builder.build(snapshot.subdivisionLevel));

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
            dst.oreVisual = src.oreVisual;
            dst.oreNoiseOffset = src.oreNoiseOffset;
            // centroid уже установлен в rebuildFromIcosphere
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
        const proc::GraphSchema& schema) {

        // Читаем входы
        const std::string snapshotJson = readStringField(readHandle, terrainSnapshotSlot);
        auto snapshot = deserializeTerrainSnapshot(
            QString::fromUtf8(snapshotJson.data(), static_cast<int>(snapshotJson.size())));

        if (!snapshot) {
            qWarning() << "DagPathBackend: failed to parse terrain snapshot";
            return proc::Commit{};
        }

        const int smoothMaxDelta = readIntField(readHandle, fieldName, smoothMaxDeltaSlot, 1);
        const int startId = readIntField(readHandle, fieldName, startCellSlot, 0);
        const int goalId = readIntField(readHandle, fieldName, goalCellSlot, 0);

        // Строим модель
        HexSphereModel model = buildModelFromSnapshot(*snapshot);

        // Строим граф и ищем путь
        PathBuilder builder(model, smoothMaxDelta);
        builder.build();
        std::vector<int> path = builder.astar(startId, goalId);

        // Формируем результат как JSON
        QJsonObject resultJson;
        resultJson["found"] = !path.empty();

        if (!path.empty()) {
            QJsonArray pathArray;
            for (int id : path) {
                pathArray.append(id);
            }
            resultJson["path"] = pathArray;

            float length = 0.0f;
            const auto& cells = model.cells();
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                length += PathBuilder::edgeAngularDistance(
                    cells[path[i]], cells[path[i + 1]]);
            }
            resultJson["length"] = length;
        }

        proc::Commit commit;
        commit.set(
            pathResultSlot,
            QString::fromUtf8(QJsonDocument(resultJson).toJson(QJsonDocument::Compact)).toStdString(),
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

    proc::RuntimeOperationRegistry buildPathRuntimeRegistry(const proc::GraphSchema& schema) {
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
            [&schema,
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
                        tsSlot, smdSlot, scSlot, gcSlot, prSlot, schema);
            });

        return registry;
    }

} // namespace

// ============================================================
// СТРУКТУРА Impl
// ============================================================

struct DagPathBackend::Impl {
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
        , runtimeRegistry(buildPathRuntimeRegistry(schema))
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
        proc::Commit c;
        c.set(
            terrainSnapshotSlot,                               // ← ИСПОЛЬЗУЕМ СЛОТ
            serializeTerrainSnapshot(snapshot).toStdString(),
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
            engine.flush_prepare(outputs);
        }
        catch (const std::exception& e) {
            qWarning() << "DagPathBackend::flush_prepare failed:" << e.what();
            return PathResult{};
        }

        auto& prepared = engine.prepared_output_store();

        PathResult result;
        auto pathView = proc::get_value_view(prepared, "pathResult");
        if (pathView) {
            std::string pathJson(pathView->data(), pathView->size());
            QJsonDocument doc = QJsonDocument::fromJson(
                QString::fromUtf8(pathJson.data(), static_cast<int>(pathJson.size())).toUtf8());

            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                result.found = obj["found"].toBool();

                if (result.found) {
                    QJsonArray pathArray = obj["path"].toArray();
                    result.cellIds.reserve(pathArray.size());
                    for (const auto& val : pathArray) {
                        result.cellIds.push_back(val.toInt());
                    }
                    result.length = static_cast<float>(obj["length"].toDouble());
                }
            }
        }

        engine.ack_outputs();

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
