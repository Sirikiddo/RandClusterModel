#pragma once

#include <QString>
#include <optional>

#include "TerrainBackendTypes.h"

/// Сериализация TerrainSnapshot в JSON-строку
QString serializeTerrainSnapshot(const TerrainSnapshot& snapshot);

/// Десериализация TerrainSnapshot из JSON-строки
std::optional<TerrainSnapshot> deserializeTerrainSnapshot(const QString& encoded);