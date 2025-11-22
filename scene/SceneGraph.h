#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include "Entity.h"

namespace scene {

class SceneGraph {
public:
    SceneGraph() = default;

    std::shared_ptr<Entity> spawn(const Entity& proto);
    std::shared_ptr<Entity> spawn(std::shared_ptr<Entity> entity);
    void destroy(int id);

    // Legacy helpers preserved for Planet and UI code.
    std::shared_ptr<Entity> addEntity(const Entity& e) { return spawn(e); }
    void removeEntity(int id) { destroy(id); }
    std::optional<std::reference_wrapper<Entity>> getEntity(int id);
    std::optional<std::reference_wrapper<Entity>> getSelectedEntity();

    void clear();
    void update(float dt);

    std::optional<std::shared_ptr<Entity>> find(int id);
    std::optional<std::shared_ptr<Entity>> selected();
    const std::vector<std::shared_ptr<Entity>>& entities() const { return entities_; }

    using SpawnListener = std::function<void(const Entity&)>;
    using DestroyListener = std::function<void(int)>;
    using UpdateListener = std::function<void(Entity&)>;

    void onSpawn(SpawnListener cb) { spawnListeners_.push_back(std::move(cb)); }
    void onDestroy(DestroyListener cb) { destroyListeners_.push_back(std::move(cb)); }
    void onUpdate(UpdateListener cb) { updateListeners_.push_back(std::move(cb)); }

private:
    int nextId_ = 0;
    std::vector<std::shared_ptr<Entity>> entities_;
    std::vector<SpawnListener> spawnListeners_;
    std::vector<DestroyListener> destroyListeners_;
    std::vector<UpdateListener> updateListeners_;
};

} // namespace scene

