#pragma once

#include "DagTerrainBackend.h"
#include "LegacyTerrainBackend.h"

using SelectedTerrainBackend = DagTerrainBackend;
inline constexpr bool kUsesDagTerrainBackend = SelectedTerrainBackend::usesDagPath;

static_assert(TerrainBackend<SelectedTerrainBackend>);
