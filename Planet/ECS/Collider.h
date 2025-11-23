#pragma once
#include <QVector3D>

namespace ecs {

struct Collider {
    float radius = 1.0f;
};

struct CollisionHit {
    QVector3D normal{0.0f, 1.0f, 0.0f};
    float penetrationDepth = 0.0f;
};

inline CollisionHit collideSpheres(const QVector3D& aPos, float aRadius, const QVector3D& bPos, float bRadius) {
    QVector3D delta = bPos - aPos;
    const float dist = delta.length();
    const float combined = aRadius + bRadius;
    CollisionHit hit;
    if (dist < combined && dist > 0.0001f) {
        hit.normal = delta.normalized();
        hit.penetrationDepth = combined - dist;
    }
    return hit;
}

} // namespace ecs
