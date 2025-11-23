#pragma once
#include <functional>

#include "Entity.h"

namespace ecs {

struct Script {
    std::function<void(EntityId, float)> onUpdate;
};

} // namespace ecs
