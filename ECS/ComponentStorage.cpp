#include "ComponentStorage.h"

#include <algorithm>

namespace ecs {

Entity& ComponentStorage::createEntity(const QString& name) {
    Entity entity;
    entity.id = nextId_++;
    entity.name = name;
    auto [it, _] = entities_.emplace(entity.id, entity);
    entityOrder_.push_back(entity.id);
    return it->second;
}

void ComponentStorage::destroyEntity(EntityId id) {
    entities_.erase(id);
    transforms_.erase(id);
    meshes_.erase(id);
    colliders_.erase(id);
    materials_.erase(id);
    scripts_.erase(id);
    entityOrder_.erase(std::remove(entityOrder_.begin(), entityOrder_.end(), id), entityOrder_.end());
}

void ComponentStorage::clear() {
    entities_.clear();
    transforms_.clear();
    meshes_.clear();
    colliders_.clear();
    materials_.clear();
    scripts_.clear();
    entityOrder_.clear();
    nextId_ = 0;
}

Entity* ComponentStorage::getEntity(EntityId id) {
    auto it = entities_.find(id);
    return (it != entities_.end()) ? &it->second : nullptr;
}

const Entity* ComponentStorage::getEntity(EntityId id) const {
    auto it = entities_.find(id);
    return (it != entities_.end()) ? &it->second : nullptr;
}

std::vector<std::reference_wrapper<const Entity>> ComponentStorage::entities() const {
    std::vector<std::reference_wrapper<const Entity>> list;
    list.reserve(entityOrder_.size());
    for (auto id : entityOrder_) {
        list.emplace_back(entities_.at(id));
    }
    return list;
}

std::optional<std::reference_wrapper<Entity>> ComponentStorage::selectedEntity() {
    for (auto id : entityOrder_) {
        auto it = entities_.find(id);
        if (it != entities_.end() && it->second.selected) {
            return it->second;
        }
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const Entity>> ComponentStorage::selectedEntity() const {
    for (auto id : entityOrder_) {
        auto it = entities_.find(id);
        if (it != entities_.end() && it->second.selected) {
            return std::cref(it->second);
        }
    }
    return std::nullopt;
}

void ComponentStorage::setSelected(EntityId id, bool value) {
    for (auto& [eid, entity] : entities_) {
        if (eid == id) {
            entity.selected = value;
        } else if (value) {
            entity.selected = false;
        }
    }
}

void ComponentStorage::update(float dt) {
    for (auto& [id, script] : scripts_) {
        if (script.onUpdate) {
            script.onUpdate(id, dt);
        }
    }
}

} // namespace ecs
