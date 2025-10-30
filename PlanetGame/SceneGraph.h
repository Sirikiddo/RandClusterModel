#pragma once
#include "SceneEntity.h"
#include <vector>
#include <optional>
#include <functional>

class SceneGraph {
public:
    SceneGraph() = default;

    SceneEntity& addEntity(const SceneEntity& e);
    void removeEntity(int id);

    std::optional<std::reference_wrapper<SceneEntity>> getEntity(int id);
    std::optional<std::reference_wrapper<SceneEntity>> getSelectedEntity();

    const std::vector<SceneEntity>& entities() const { return entities_; }
    void clear();

private:
    std::vector<SceneEntity> entities_;
    int nextId_ = 0;
};