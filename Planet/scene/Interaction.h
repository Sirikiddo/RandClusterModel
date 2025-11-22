#pragma once
#include <functional>
#include <QVector3D>
#include "Transform.h"

namespace scene {

struct CollisionInfo {
    QVector3D normal{0.0f, 1.0f, 0.0f};
    float penetrationDepth = 0.0f;
};

class ICollider {
public:
    virtual ~ICollider() = default;
    virtual CollisionInfo collide(const Transform& self, const ICollider& other, const Transform& otherTransform) const = 0;
};

class SphereCollider : public ICollider {
public:
    explicit SphereCollider(float radius) : radius_(radius) {}
    CollisionInfo collide(const Transform& self, const ICollider& other, const Transform& otherTransform) const override;
    float radius() const { return radius_; }
private:
    float radius_ = 1.0f;
};

class ILightReceiver {
public:
    virtual ~ILightReceiver() = default;
    virtual QVector3D albedo() const = 0;
};

class LambertReceiver : public ILightReceiver {
public:
    explicit LambertReceiver(const QVector3D& color) : color_(color) {}
    QVector3D albedo() const override { return color_; }
private:
    QVector3D color_;
};

class IPhysicalBody {
public:
    virtual ~IPhysicalBody() = default;
    virtual void integrate(Transform& transform, float dt) = 0;
};

class KinematicBody : public IPhysicalBody {
public:
    explicit KinematicBody(const QVector3D& velocity = {}) : velocity_(velocity) {}
    void integrate(Transform& transform, float dt) override {
        transform.position += velocity_ * dt;
    }
    void setVelocity(const QVector3D& v) { velocity_ = v; }
    QVector3D velocity() const { return velocity_; }
private:
    QVector3D velocity_;
};

inline CollisionInfo SphereCollider::collide(const Transform& self, const ICollider& other, const Transform& otherTransform) const {
    const SphereCollider* sphereOther = dynamic_cast<const SphereCollider*>(&other);
    if (!sphereOther) {
        return {};
    }
    QVector3D delta = otherTransform.position - self.position;
    float dist = delta.length();
    float combined = radius_ + sphereOther->radius_;
    CollisionInfo info;
    if (dist < combined && dist > 0.0001f) {
        info.normal = delta.normalized();
        info.penetrationDepth = combined - dist;
    }
    return info;
}

} // namespace scene

