#include <type_traits>
#include <vector>

#include "EngineFacade.h"
#include "TerrainBackendSelector.h"

static_assert(TerrainBackend<SelectedTerrainBackend>);
static_assert(std::is_default_constructible_v<SelectedTerrainBackend>);
static_assert(kUsesDagTerrainBackend == SelectedTerrainBackend::usesDagPath);
static_assert(requires(EngineFacade& engine, const TerrainMesh& mesh, const QVector3D& cameraPos) {
    engine.setVisibilityMesh(mesh);
    { engine.currentTerrainMesh() } -> std::same_as<const TerrainMesh*>;
    { engine.prepareVisibleTerrainIndices(cameraPos) } -> std::same_as<bool>;
    { engine.currentVisibleTerrainIndices() } -> std::same_as<const std::vector<uint32_t>*>;
});
