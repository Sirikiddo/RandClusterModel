#include "TerrainSerialization.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// Вспомогательные функции (остаются в анонимном namespace)
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

} // namespace

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