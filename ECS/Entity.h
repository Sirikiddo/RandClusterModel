#pragma once
#include <QString>

namespace ecs {

using EntityId = int;

struct Entity {
    EntityId id = -1;
    QString name{};
    int currentCell = -1;
    bool selected = false;
};

} // namespace ecs
