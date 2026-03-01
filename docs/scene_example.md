# ECS example

This document outlines a minimal workflow for creating entities, attaching core components, and driving an update loop with the new ECS storage.

```cpp
#include "ECS/ComponentStorage.h"

ecs::ComponentStorage ecs;
ecs::CoordinateFrame world;

auto& planet = ecs.createEntity("Planet");
ecs.emplace<ecs::Mesh>(planet.id).meshId = "hexSphere";
ecs::Transform& planetTransform = ecs.emplace<ecs::Transform>(planet.id);
planetTransform.scale = { 2.0f, 2.0f, 2.0f };

auto& rover = ecs.createEntity("Rover");
ecs.emplace<ecs::Mesh>(rover.id).meshId = "pyramid";
ecs::Transform& roverTransform = ecs.emplace<ecs::Transform>(rover.id);
roverTransform.position = ecs::localToWorldPoint(roverTransform, world, {1.0f, 0.0f, 0.0f});
ecs.setSelected(rover.id, true);

auto& script = ecs.emplace<ecs::Script>(rover.id);
script.onUpdate = [&](ecs::EntityId id, float dt) {
    Q_UNUSED(dt);
    /* physics, AI, etc. */
};
ecs.update(0.016f);
```

* Global coordinate system is right-handed with **Y** up and meters as the default unit.
* `Transform` stores position, orientation, and scale and can convert local points to world space.
* `ComponentStorage` owns entities and their components, providing simple iteration (`each`) and an update pass for script callbacks.
