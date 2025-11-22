# Scene graph example

This document outlines a minimal workflow for creating a shared scene graph, registering a planet entity and a few helpers, and driving an update loop.

```cpp
#include "scene/SceneGraph.h"

scene::SceneGraph scene;
scene::CoordinateFrame world;

scene::Entity planet("Planet", "hexSphere");
planet.transform().scale = { 2.0f, 2.0f, 2.0f };
scene.spawn(planet);

scene::Entity rover("Rover", "pyramid");
rover.transform().position = scene::localToWorldPoint(rover.transform(), world, {1.0f, 0.0f, 0.0f});
scene.spawn(rover);

scene.onUpdate([](scene::Entity& e){ /* physics, AI, etc. */ });
scene.update(0.016f);
```

* Global coordinate system is right-handed with **Y** up and meters as the default unit.
* `Transform` stores position, orientation, and scale and can convert local points to world space.
* The `SceneGraph` provides spawn/destroy events and update callbacks so disparate systems (physics, UI) can react consistently.
