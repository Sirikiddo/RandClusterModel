# Codex task: refactor the real-time rendering pipeline

> Status: completed. This document is retained as historical implementation context; `README.md` describes the current renderer contract.

This is the implementation brief for Codex to untangle the current rendering path. Keep changes inside the `Planet` folder and update Visual Studio project files if you add or move sources.

## Problem summary
* Surface placement math is duplicated in `HexSphereWidget::getSurfacePoint` and `HexSphereRenderer::getSurfacePoint`, which drift in signature/behavior.
* Buffer uploads are orchestrated manually by `HexSphereWidget::uploadBuffers()`, even though all inputs are already produced by `HexSphereSceneController`.
* Buffer usage flags (`useStaticBuffers_`, `terrainBufferUsage_`, `wireBufferUsage_`) live on the widget but are not honored by the renderer when creating VBO/IBO data.
* `HexSphereRenderer::render()` mixes terrain, water, selection, debug wire, path, and tree rendering without clear pass boundaries or state ownership.

## What to change (implementation map)
1) **Unify surface placement**: create a shared helper (small header/inline ok) that returns a height-aware surface point for a cell. Replace the widget copy (`HexSphereWidget.cpp:getSurfacePoint`) and renderer copy (`HexSphereRenderer.cpp:getSurfacePoint`) with this helper so both call the same math. Keep heightStep/offset support.

2) **Centralize uploads**: add a single entry point on `HexSphereRenderer` (`uploadScene(const HexSphereSceneController&, const UploadOptions&)`) that uploads the scene in the right order and wraps `makeCurrent()/doneCurrent()`. The topology-dependent water proxy is owned by the scene and uploaded internally only when its revision changes; there is no public `uploadWater` path.

3) **Own buffer usage configuration**: thread the buffer usage choice into the renderer (or let it own the decision) so the actual GL buffer creation respects the widget’s strategy. Remove or consolidate unused flags; ensure `useStaticBuffers_`, `terrainBufferUsage_`, and `wireBufferUsage_` no longer exist as dead state.

4) **Reorganize render passes**: split `HexSphereRenderer::render()` into named phases with clear state setup/teardown (depth mask, blending, shader program). Suggested grouping: terrain -> water -> selection/outline & wire/path -> entities -> instanced/looped tree models. The goal is to minimize GL state churn and make adding new draw calls obvious.

## Deliverables & acceptance criteria
* Only one surface-position implementation is used by both widget and renderer.
* Widget delegates buffer uploading to one renderer method; ordering and GL context guards live in the renderer.
* The chosen buffer usage strategy is configured once and applied during buffer creation; stale flags are removed or documented.
* `render()` is organized into explicit passes with comments or helpers marking each phase and responsible state changes.
* Any new helper/source files are registered in `Planet.vcxproj` and `.filters` so Visual Studio sees them.
