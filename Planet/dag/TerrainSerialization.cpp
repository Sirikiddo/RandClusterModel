#include "TerrainSerialization.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {
constexpr int kCurrentTerrainSnapshotVersion = 2;

bool isSupportedOreType(int value) {
    return value >= static_cast<int>(OreType::None)
        && value <= static_cast<int>(OreType::Diamond);
}

bool isSupportedBiome(int value) {
    return value >= static_cast<int>(Biome::Sea)
        && value <= static_cast<int>(Biome::Jungle);
}

bool isIntegralNumber(const QJsonValue& value) {
    return value.isDouble() && value.toDouble() == static_cast<double>(value.toInt());
}

bool hasNumericCellFields(const QJsonObject& entry) {
    return entry["height"].isDouble()
        && entry["biome"].isDouble()
        && entry["temperature"].isDouble()
        && entry["humidity"].isDouble()
        && entry["pressure"].isDouble()
        && entry["oreDensity"].isDouble()
        && entry["oreType"].isDouble();
}
} // namespace

QString serializeTerrainSnapshot(const TerrainSnapshot& snapshot) {
    QJsonObject root;
    root["version"] = kCurrentTerrainSnapshotVersion;
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
        cells.push_back(entry);
    }
    root["cells"] = cells;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

std::optional<TerrainSnapshot> deserializeTerrainSnapshot(const QString& encoded) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(encoded.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    if (!isIntegralNumber(root["version"])) {
        return std::nullopt;
    }
    const int version = root["version"].toInt();
    if (version != 1 && version != kCurrentTerrainSnapshotVersion) {
        return std::nullopt;
    }
    if (!root["subdivisionLevel"].isDouble()
        || !root["generatorIndex"].isDouble()
        || !root["seed"].isDouble()
        || !root["seaLevel"].isDouble()
        || !root["scale"].isDouble()
        || !root["cells"].isArray()) {
        return std::nullopt;
    }

    TerrainSnapshot snapshot;
    snapshot.subdivisionLevel = root["subdivisionLevel"].toInt();
    snapshot.generatorIndex = root["generatorIndex"].toInt();
    snapshot.params.seed = static_cast<uint32_t>(root["seed"].toInteger());
    snapshot.params.seaLevel = root["seaLevel"].toInt();
    snapshot.params.scale = static_cast<float>(root["scale"].toDouble());

    const QJsonArray cells = root["cells"].toArray();
    snapshot.cells.reserve(static_cast<size_t>(cells.size()));
    for (const auto& value : cells) {
        if (!value.isObject()) {
            return std::nullopt;
        }
        const QJsonObject entry = value.toObject();
        if (!hasNumericCellFields(entry)) {
            return std::nullopt;
        }

        if (!isIntegralNumber(entry["biome"]) || !isIntegralNumber(entry["oreType"])) {
            return std::nullopt;
        }
        const int biome = entry["biome"].toInt(-1);
        const int oreType = entry["oreType"].toInt(-1);
        const double oreDensity = entry["oreDensity"].toDouble(-1.0);
        if (!isSupportedBiome(biome)
            || !isSupportedOreType(oreType)
            || oreDensity < 0.0
            || oreDensity > 1.0) {
            return std::nullopt;
        }

        TerrainCellSnapshot cell;
        cell.height = entry["height"].toInt();
        cell.biome = static_cast<Biome>(biome);
        cell.temperature = static_cast<float>(entry["temperature"].toDouble());
        cell.humidity = static_cast<float>(entry["humidity"].toDouble());
        cell.pressure = static_cast<float>(entry["pressure"].toDouble());
        cell.oreDensity = static_cast<float>(oreDensity);
        cell.oreType = static_cast<OreType>(oreType);
        snapshot.cells.push_back(cell);
    }

    return snapshot;
}
