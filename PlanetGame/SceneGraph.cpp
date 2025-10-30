#include "SceneGraph.h"

SceneEntity& SceneGraph::addEntity(const SceneEntity& e) {
    SceneEntity copy = e;
    copy.id = nextId_++;
    entities_.push_back(std::move(copy));
    return entities_.back();
}

void SceneGraph::removeEntity(int id) {
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(),
        [&](const SceneEntity& e) { return e.id == id; }),
        entities_.end());
}

std::optional<std::reference_wrapper<SceneEntity>> SceneGraph::getEntity(int id) {
    for (auto& e : entities_)
        if (e.id == id) return e;
    return std::nullopt;
}

std::optional<std::reference_wrapper<SceneEntity>> SceneGraph::getSelectedEntity() {
    for (auto& e : entities_)
        if (e.selected) return e;
    return std::nullopt;
}

void SceneGraph::clear() {
    entities_.clear();
    nextId_ = 0;
}