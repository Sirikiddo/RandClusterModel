#pragma once
#include <QVector3D>

namespace ecs {

struct Material {
    QVector3D albedo{1.0f, 1.0f, 1.0f};
    bool useTexture = false;
};

} // namespace ecs
