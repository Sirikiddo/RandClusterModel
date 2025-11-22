#include "Entity.h"

namespace scene {

namespace {

std::unique_ptr<ICollider> cloneCollider(const ICollider* src) {
    if (!src) return nullptr;
    if (const auto* sphere = dynamic_cast<const SphereCollider*>(src)) {
        return std::make_unique<SphereCollider>(*sphere);
    }
    return nullptr;
}

std::unique_ptr<ILightReceiver> cloneLight(const ILightReceiver* src) {
    if (!src) return nullptr;
    if (const auto* lambert = dynamic_cast<const LambertReceiver*>(src)) {
        return std::make_unique<LambertReceiver>(*lambert);
    }
    return nullptr;
}

std::unique_ptr<IPhysicalBody> cloneBody(const IPhysicalBody* src) {
    if (!src) return nullptr;
    if (const auto* kinematic = dynamic_cast<const KinematicBody*>(src)) {
        return std::make_unique<KinematicBody>(*kinematic);
    }
    return nullptr;
}

} // namespace

Entity::Entity(const QString& name, const QString& meshId)
    : name_(name), meshId_(meshId) {}

Entity::Entity(const Entity& other)
    : id_(other.id_),
      name_(other.name_),
      meshId_(other.meshId_),
      transform_(other.transform_),
      currentCell_(other.currentCell_),
      selected_(other.selected_),
      collider_(cloneCollider(other.collider_.get())),
      lightReceiver_(cloneLight(other.lightReceiver_.get())),
      body_(cloneBody(other.body_.get())) {}

Entity& Entity::operator=(const Entity& other) {
    if (this == &other) return *this;
    id_ = other.id_;
    name_ = other.name_;
    meshId_ = other.meshId_;
    transform_ = other.transform_;
    currentCell_ = other.currentCell_;
    selected_ = other.selected_;
    collider_ = cloneCollider(other.collider_.get());
    lightReceiver_ = cloneLight(other.lightReceiver_.get());
    body_ = cloneBody(other.body_.get());
    return *this;
}

void Entity::update(float dt) {
    // Простое базовое обновление: если есть физическое тело, применяем силы.
    if (body_) {
        body_->integrate(transform_, dt);
    }
}

} // namespace scene

