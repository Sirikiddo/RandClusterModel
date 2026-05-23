#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <QString>
#include <QVector3D>

#include "TerrainBackendTypes.h"
#include "generation/MeshGenerators/TerrainMeshPolicy.h"

namespace terrain_dag {

QString serializeTerrainSnapshot(const TerrainSnapshot& snapshot);
std::optional<TerrainSnapshot> deserializeTerrainSnapshot(const QString& encoded);

TerrainSnapshot buildTerrainSnapshot(int generatorIndex, int subdivisionLevel, const TerrainParams& params);

TerrainMesh buildTerrainMeshFromSnapshot(const TerrainSnapshot& snapshot, const TerrainRenderConfig& renderConfig);

QString serializeCameraPos(const QVector3D& cameraPos);
QVector3D deserializeCameraPos(std::string_view encoded);

std::string summarizeVisibleTerrainIndices(const std::vector<uint32_t>& indices);

} // namespace terrain_dag
