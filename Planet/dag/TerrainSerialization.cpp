#include "TerrainSerialization.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {
constexpr int kCurrentTerrainSnapshotVersion = 3;

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

    // v3: клетки, занятые деревьями. Дорога не может проходить через них и
    // заканчиваться на них, поэтому набор едет вместе с террейном.
    QJsonArray treeCells;
    for (int cellId : snapshot.treeCells) {
        treeCells.push_back(cellId);
    }
    root["treeCells"] = treeCells;

    // Клетки русел рек — ещё одно препятствие для дороги, того же рода, что и
    // деревья. Поле необязательное: снапшоты v3, записанные до появления рек,
    // читаются как «рек нет».
    QJsonArray riverCells;
    for (int cellId : snapshot.riverCells) {
        riverCells.push_back(cellId);
    }
    root["riverCells"] = riverCells;

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
    if (version != 1 && version != 2 && version != kCurrentTerrainSnapshotVersion) {
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

    // v3: клетки с деревьями. Для старых снапшотов (v1/v2) поля нет — это пустой
    // набор; для текущей версии оно обязательно.
    const QJsonValue treeCellsValue = root["treeCells"];
    if (treeCellsValue.isArray()) {
        const QJsonArray treeCells = treeCellsValue.toArray();
        snapshot.treeCells.reserve(static_cast<size_t>(treeCells.size()));
        for (const auto& value : treeCells) {
            if (!isIntegralNumber(value) || value.toInt() < 0) {
                return std::nullopt;
            }
            const int cellId = value.toInt();
            if (cellId >= static_cast<int>(snapshot.cells.size())) {
                return std::nullopt;
            }
            snapshot.treeCells.push_back(cellId);
        }
    }
    else if (version >= kCurrentTerrainSnapshotVersion) {
        return std::nullopt;
    }

    // Клетки русел рек: необязательное поле (старые v3 его не содержат).
    const QJsonValue riverCellsValue = root["riverCells"];
    if (riverCellsValue.isArray()) {
        const QJsonArray riverCells = riverCellsValue.toArray();
        snapshot.riverCells.reserve(static_cast<size_t>(riverCells.size()));
        for (const auto& value : riverCells) {
            if (!isIntegralNumber(value) || value.toInt() < 0) {
                return std::nullopt;
            }
            const int cellId = value.toInt();
            if (cellId >= static_cast<int>(snapshot.cells.size())) {
                return std::nullopt;
            }
            snapshot.riverCells.push_back(cellId);
        }
    }
    else if (!riverCellsValue.isUndefined() && !riverCellsValue.isNull()) {
        return std::nullopt;
    }

    return snapshot;
}
