# Planet architecture and layout

The Planet application is a Qt + OpenGL playground for rendering and interacting with a procedural hex-sphere world. The codebase is split into small subsystems so camera/input handling, scene building, rendering, and generation can evolve independently.

## Runtime pipeline
- **Bootstrap:** `core/main.cpp` configures a 3.3 core profile with multisampling and launches `MainWindow`, which hosts the entire UI stack.【F:core/main.cpp†L1-L26】
- **UI shell and controls:** `ui/MainWindow` wires together the `CameraController`, the central `HexSphereWidget`, and dockable settings. It exposes subdivision, reset, and selection actions plus a docked `PlanetSettingsPanel` for generator controls and visualization toggles.【F:ui/MainWindow.cpp†L1-L66】
- **Widget event surface:** `ui/HexSphereWidget` owns the GL viewport, translates Qt input events into controller calls, drives a HUD label, and runs a timer to advance animated water.【F:ui/HexSphereWidget.cpp†L1-L76】【F:ui/HexSphereWidget.cpp†L88-L113】
- **Input, scene, and ECS orchestration:** `controllers/InputController` is the hub for interaction. It initializes the OpenGL function set and `HexSphereRenderer`, seeds the ECS with a default explorer entity on the planet surface, rebuilds GPU buffers when the model changes, and keeps track of buffer usage strategy. Every frame it composes a `RenderGraph`, `RenderCamera`, and `SceneLighting` bundle for the renderer.【F:controllers/InputController.cpp†L38-L83】【F:controllers/InputController.cpp†L89-L98】【F:controllers/InputController.cpp†L324-L372】
- **GPU rendering back end:** `renderers/HexSphereRenderer` owns the GL programs, VAOs/VBOs, and delegate renderers for terrain, water, entities, and overlays. It exposes `uploadScene` for bulk buffer updates (wireframe, terrain, selection outline, path, and water) and `renderScene` for pass-driven drawing using the camera and lighting data.【F:renderers/HexSphereRenderer.h†L13-L119】
- **Procedural generation and geometry:** `generation/` hosts terrain generator implementations (`NoOp`, `Sine`, `Perlin`, and climate-based biomes) plus mesh builders for terrain, water, wireframe, and selection outlines. These feed CPU-side `HexSphereModel` data that later gets uploaded to the GPU.【F:generation/TerrainGenerator.h†L1-L34】【F:generation/MeshGenerators/WaterMeshGenerator.h†L1-L17】
- **Data models and scene storage:** `model/` contains the `HexSphereModel`, OBJ loading via `ModelHandler`, and placement helpers, while `ECS/ComponentStorage` owns entities and components (transform, mesh, collider, material, script) with selection support and update hooks. The lightweight `scene/` folder keeps generic scene graph helpers used alongside the ECS layer.【F:model/ModelHandler.h†L1-L42】【F:ECS/ComponentStorage.h†L1-L63】【F:scene/SceneGraph.h†L1-L42】

## Directory overview
- `core/` — application entry point and global GL surface format setup.【F:core/main.cpp†L1-L26】
- `controllers/` — input/state controllers and helpers for driving the planet scene (`CameraController.*`, `InputController.*`, `HexSphereSceneController.*`, `PathBuilder.*`).【F:controllers/InputController.h†L1-L68】
- `ui/` — Qt widgets and overlays for the GL viewport, HUD, settings panel, and performance stats (`HexSphereWidget.*`, `MainWindow.*`, `OverlayRenderer.*`, `PerformanceStats.*`, `PlanetSettingsPanel.*`).【F:ui/HexSphereWidget.h†L1-L46】【F:ui/MainWindow.cpp†L1-L66】
- `renderers/` — OpenGL renderers and tessellation utilities (`HexSphereRenderer.*`, `EntityRenderer.*`, `TerrainRenderer.*`, `WaterRenderer.*`, `TerrainTessellator.*`).【F:renderers/HexSphereRenderer.h†L13-L119】【F:renderers/TerrainTessellator.h†L1-L38】
- `generation/` — procedural content and mesh builders, including `MeshGenerators/` for terrain, water, wireframe, and selection outlines.【F:generation/TerrainGenerator.h†L1-L34】【F:generation/MeshGenerators/WaterMeshGenerator.h†L1-L17】
- `model/` — planet data structures, surface placement math, and shared model loading (`HexSphereModel.*`, `ModelHandler.*`, `SurfacePlacement.h`, `SceneEntity.h`, `simple3d_parser.hpp`).【F:model/ModelHandler.h†L1-L42】
- `ECS/` — lightweight entity/component storage for transforms, meshes, colliders, materials, scripts, and selection tracking.【F:ECS/ComponentStorage.h†L1-L63】
- `scene/` — foundational scene graph types (`Entity.*`, `SceneGraph.*`, `Transform.h`, `Interaction.h`) used alongside ECS utilities.【F:scene/SceneGraph.h†L1-L42】
- `resources/` — static assets and Qt resource list (`Planet.qrc`, shader headers, OBJ samples, NuGet packages file).【F:resources/Planet.qrc†L1-L4】
- `tools/` — developer utilities and data converters (e.g., `model_cache_test.cpp`, `converters/DataAdapters.*`).【F:tools/model_cache_test.cpp†L1-L12】【F:tools/converters/DataAdapters.h†L1-L36】
- `docs/` — development notes and refactor briefs for the rendering and scene subsystems.【F:docs/planet_reorg_tasks.md†L1-L15】
- `tests/` — integration and scene-level experiments (`scene_integration.cpp`).【F:tests/scene_integration.cpp†L1-L40】

## Project files and adding content
- Project files (`Planet.vcxproj`, `Planet.vcxproj.filters`, `Planet.vcxproj.user`) live at the root of `Planet/`; keep them in sync with any file moves so Visual Studio mirrors the folder structure.
- Place new sources beside the subsystem they belong to (controllers, ui, renderers, generation, model, ECS/scene, tools, or resources). Include headers using folder-qualified paths from the project root (e.g., `#include "renderers/TerrainRenderer.h"`).
- Register new sources, headers, or assets in `Planet.vcxproj` and the matching filter file so they appear in Solution Explorer and are bundled with the build.
