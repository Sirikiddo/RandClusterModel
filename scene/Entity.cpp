#include "Entity.h"

namespace scene {

Entity::Entity(const QString& name, const QString& meshId)
    : name_(name), meshId_(meshId) {}

void Entity::update(float dt) {
    // Простое базовое обновление: если есть физическое тело, применяем силы.
    if (body_) {
        body_->integrate(transform_, dt);
    }
}

} // namespace scene

