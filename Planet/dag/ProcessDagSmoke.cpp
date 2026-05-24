#include <type_traits>

#include "TerrainBackendSelector.h"

static_assert(TerrainBackend<SelectedTerrainBackend>);
static_assert(std::is_default_constructible_v<SelectedTerrainBackend>);
static_assert(kUsesDagTerrainBackend == SelectedTerrainBackend::usesDagPath);
