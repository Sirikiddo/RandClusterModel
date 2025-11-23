# Planet project layout

The Planet sources are now organized by responsibility so that controllers, rendering code, UI, and generation logic live in separate folders. Use this layout when adding or modifying files.

## Directory overview
- `core/` — application entry point (`main.cpp`).
- `controllers/` — user input, camera control, scene orchestration, and path building (`CameraController.*`, `InputController.*`, `HexSphereSceneController.*`, `PathBuilder.*`).
- `ui/` — Qt widgets, panels, overlays, and performance HUD (`MainWindow.*`, `HexSphereWidget.*`, `PlanetSettingsPanel.*`, `OverlayRenderer.*`, `PerformanceStats.*`).
- `renderers/` — OpenGL renderers and tessellation helpers (`HexSphereRenderer.*`, `EntityRenderer.*`, `TerrainRenderer.*`, `WaterRenderer.*`, `TerrainTessellator.*`).
- `generation/` — procedural content (terrain generators, biome/noise helpers, mesh builders) including `MeshGenerators/`.
- `model/` — data models, loaders, and geometry helpers (`HexSphereModel.*`, `ModelHandler.*`, `SurfacePlacement.h`, `SceneEntity.h`, `DebugModel.h`, `simple3d_parser.hpp`).
- `resources/` — static assets and Qt resources (`Planet.qrc`, `HexSphereWidget_shaders.h`, `tree.obj`, `packages.config`).
- `tools/` — developer utilities (`model_cache_test.cpp`, `converters/DataAdapters.*`).
- `ECS/` and `scene/` — existing entity/component and scene graph helpers (unchanged).
- `tests/` — test assets (unchanged).
- Project files: `Planet.vcxproj`, `Planet.vcxproj.filters`, and `Planet.vcxproj.user` stay at the repository root of `Planet/`.

## Build and project metadata
- `Planet.vcxproj` and `Planet.vcxproj.filters` now reference the new paths and mirror the folder structure in Solution Explorer.
- The project includes the repository root as an additional include directory so headers can be referenced with their folder prefix (e.g., `controllers/CameraController.h`).

## Adding or moving files
1. Place new sources in the folder that matches their role (controllers, ui, renderers, generation, model, resources, or tools).
2. Include headers using their folder-qualified path from the project root (e.g., `#include "renderers/TerrainRenderer.h"`).
3. Register the files in `Planet.vcxproj` and update `Planet.vcxproj.filters` so Visual Studio picks them up in the correct virtual folder.
4. If assets or shader headers are added, keep them under `resources/` and add them to the project as `None` or `QtRcc` items as appropriate.
