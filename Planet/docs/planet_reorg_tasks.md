# Planet source reorganization summary

The Planet sources have been moved into topic-focused folders so that controllers, rendering, UI, generation, and model code are separated. Project metadata and documentation were refreshed to match.

## Completed steps
- Created `core/`, `controllers/`, `ui/`, `renderers/`, `generation/`, `model/`, `resources/`, and `tools/` directories and relocated the corresponding sources.
- Updated `Planet.vcxproj` and `Planet.vcxproj.filters` to point to the new paths and mirror the folder tree in Solution Explorer.
- Adjusted `#include` directives to use folder-qualified paths alongside an include path that anchors at the project root.
- Refreshed `README.md` with the new architecture and guidance on adding files.

## Maintenance checklist
- Place new files in the folder that matches their role and include headers using folder-qualified paths.
- Add new sources or assets to `Planet.vcxproj` and the matching filter so Visual Studio can find them.
- Keep shader headers and other static assets under `resources/` and register them as `None` or `QtRcc` items as needed.
- Verify builds after structural changes to ensure no stale paths remain in the project files.
