#pragma once
#include <optional>
#include <unordered_map>
#include <vector>

#include "Entity.h"
#include "Transform.h"
#include "Mesh.h"
#include "Collider.h"
#include "Material.h"
#include "Script.h"

namespace ecs {

class ComponentStorage {
public:
    Entity& createEntity(const QString& name = QString());
    void destroyEntity(EntityId id);
    void clear();

    Entity* getEntity(EntityId id);
    const Entity* getEntity(EntityId id) const;

    std::vector<std::reference_wrapper<const Entity>> entities() const;

    template<typename Component, typename... Args>
    Component& emplace(EntityId id, Args&&... args) {
        auto& map = mapFor<Component>();
        auto [it, inserted] = map.try_emplace(id, std::forward<Args>(args)...);
        if (!inserted) {
            it->second = Component{std::forward<Args>(args)...};
        }
        return it->second;
    }

    template<typename Component>
    Component* get(EntityId id) {
        auto& map = mapFor<Component>();
        auto it = map.find(id);
        return (it != map.end()) ? &it->second : nullptr;
    }

    template<typename Component>
    const Component* get(EntityId id) const {
        const auto& map = mapFor<Component>();
        auto it = map.find(id);
        return (it != map.end()) ? &it->second : nullptr;
    }

    template<typename... Components, typename Func>
    void each(Func&& fn) {
        for (const auto& id : entityOrder_) {
            Entity& entity = entities_.at(id);
            if ((hasComponent<Components>(id) && ...)) {
                fn(entity, *get<Components>(id)...);
            }
        }
    }

    template<typename... Components, typename Func>
    void each(Func&& fn) const {
        for (const auto& id : entityOrder_) {
            const Entity& entity = entities_.at(id);
            if ((hasComponent<Components>(id) && ...)) {
                fn(entity, *get<Components>(id)...);
            }
        }
    }

    std::optional<std::reference_wrapper<Entity>> selectedEntity();
    std::optional<std::reference_wrapper<const Entity>> selectedEntity() const;
    void setSelected(EntityId id, bool value);

    void update(float dt);

private:
    template<typename Component>
    std::unordered_map<EntityId, Component>& mapFor();

    template<typename Component>
    const std::unordered_map<EntityId, Component>& mapFor() const;

    template<typename Component>
    bool hasComponent(EntityId id) const {
        const auto& map = mapFor<Component>();
        return map.find(id) != map.end();
    }

    EntityId nextId_ = 0;
    std::unordered_map<EntityId, Entity> entities_;
    std::vector<EntityId> entityOrder_;
    std::unordered_map<EntityId, Transform> transforms_;
    std::unordered_map<EntityId, Mesh> meshes_;
    std::unordered_map<EntityId, Collider> colliders_;
    std::unordered_map<EntityId, Material> materials_;
    std::unordered_map<EntityId, Script> scripts_;
};

// Template specializations to fetch component maps.
template<>
inline std::unordered_map<EntityId, Transform>& ComponentStorage::mapFor<Transform>() { return transforms_; }

template<>
inline std::unordered_map<EntityId, Mesh>& ComponentStorage::mapFor<Mesh>() { return meshes_; }

template<>
inline std::unordered_map<EntityId, Collider>& ComponentStorage::mapFor<Collider>() { return colliders_; }

template<>
inline std::unordered_map<EntityId, Material>& ComponentStorage::mapFor<Material>() { return materials_; }

template<>
inline std::unordered_map<EntityId, Script>& ComponentStorage::mapFor<Script>() { return scripts_; }

// Const overloads.
template<>
inline const std::unordered_map<EntityId, Transform>& ComponentStorage::mapFor<Transform>() const { return transforms_; }

template<>
inline const std::unordered_map<EntityId, Mesh>& ComponentStorage::mapFor<Mesh>() const { return meshes_; }

template<>
inline const std::unordered_map<EntityId, Collider>& ComponentStorage::mapFor<Collider>() const { return colliders_; }

template<>
inline const std::unordered_map<EntityId, Material>& ComponentStorage::mapFor<Material>() const { return materials_; }

template<>
inline const std::unordered_map<EntityId, Script>& ComponentStorage::mapFor<Script>() const { return scripts_; }

} // namespace ecs
