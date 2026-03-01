#include "SceneGraph.h"
#include <algorithm>

namespace scene {

std::shared_ptr<Entity> SceneGraph::spawn(const Entity& proto) {
    auto copy = std::make_shared<Entity>(proto);
    copy->setId(nextId_++);
    entities_.push_back(copy);
    for (const auto& cb : spawnListeners_) cb(*copy);
    return copy;
}

std::shared_ptr<Entity> SceneGraph::spawn(std::shared_ptr<Entity> entity) {
    if (!entity) return nullptr;
    entity->setId(nextId_++);
    entities_.push_back(entity);
    for (const auto& cb : spawnListeners_) cb(*entity);
    return entity;
}

void SceneGraph::destroy(int id) {
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(), [&](const auto& e) {
        if (e->id() == id) {
            for (const auto& cb : destroyListeners_) cb(id);
            return true;
        }
        return false;
    }), entities_.end());
}

void SceneGraph::clear() {
    for (const auto& e : entities_) {
        for (const auto& cb : destroyListeners_) cb(e->id());
    }
    entities_.clear();
    nextId_ = 0;
}

void SceneGraph::update(float dt) {
    for (auto& e : entities_) {
        e->update(dt);
        for (const auto& cb : updateListeners_) cb(*e);
    }
}

std::optional<std::shared_ptr<Entity>> SceneGraph::find(int id) {
    for (auto& e : entities_) {
        if (e->id() == id) return e;
    }
    return std::nullopt;
}

std::optional<std::shared_ptr<Entity>> SceneGraph::selected() {
    for (auto& e : entities_) {
        if (e->selected()) return e;
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<Entity>> SceneGraph::getEntity(int id) {
    auto found = find(id);
    if (found) return *found.value();
    return std::nullopt;
}

std::optional<std::reference_wrapper<Entity>> SceneGraph::getSelectedEntity() {
    auto found = selected();
    if (found) return *found.value();
    return std::nullopt;
}

} // namespace scene

