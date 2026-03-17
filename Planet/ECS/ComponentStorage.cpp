#include "ComponentStorage.h"
#include "Animation.h"  // <-- ДОБАВИТЬ ЭТУ СТРОКУ

#include <algorithm>

namespace ecs {

    static QVector3D samplePolylineByDistance(const Animation& anim, float distance) {
        if (anim.pathPoints.empty()) return anim.startPos;
        if (anim.pathPoints.size() == 1) return anim.pathPoints.front();
        if (anim.pathTotalLength <= 1e-6f) return anim.pathPoints.back();

        distance = std::clamp(distance, 0.0f, anim.pathTotalLength);

        // pathCumulative is expected to be same size as pathPoints
        const auto& cum = anim.pathCumulative;
        const auto& pts = anim.pathPoints;
        if (cum.size() != pts.size() || cum.size() < 2) {
            // fallback: no cumulative info
            return pts.back();
        }

        // Find first index with cum[i] >= distance
        size_t i = 1;
        while (i < cum.size() && cum[i] < distance) ++i;
        if (i >= cum.size()) return pts.back();

        const float d0 = cum[i - 1];
        const float d1 = cum[i];
        const float denom = (d1 - d0);
        const float localT = (denom > 1e-6f) ? ((distance - d0) / denom) : 0.0f;
        return pts[i - 1] * (1.0f - localT) + pts[i] * localT;
    }

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
        animations_.erase(id);  // <-- ДОБАВИТЬ ЭТУ СТРОКУ
        entityOrder_.erase(std::remove(entityOrder_.begin(), entityOrder_.end(), id), entityOrder_.end());
    }

    void ComponentStorage::clear() {
        entities_.clear();
        transforms_.clear();
        meshes_.clear();
        colliders_.clear();
        materials_.clear();
        scripts_.clear();
        animations_.clear();  // <-- ДОБАВИТЬ ЭТУ СТРОКУ
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
            }
            else if (value) {
                entity.selected = false;
            }
        }
    }

    void ComponentStorage::update(float dt) {
        // Обновляем скрипты
        for (auto& [id, script] : scripts_) {
            if (script.onUpdate) {
                script.onUpdate(id, dt);
            }
        }

        // Обновляем анимации
        std::vector<EntityId> toRemove;
        for (auto& [id, anim] : animations_) {
            anim.elapsed += dt;

            if (anim.isFinished()) {
                // Завершаем анимацию
                if (anim.onComplete) {
                    anim.onComplete(id);
                }
                toRemove.push_back(id);
                continue;
            }

            float t = anim.progress();

            // Применяем анимацию в зависимости от типа
            if (auto* transform = get<Transform>(id)) {
                switch (anim.type) {
                case Animation::Type::MoveTo: {
                    // Используем easing функцию для плавности
                    float eased = Animation::easeOutCubic(t);
                    if (!anim.pathPoints.empty()) {
                        const float d = eased * anim.pathTotalLength;
                        transform->position = samplePolylineByDistance(anim, d);
                    }
                    else {
                        transform->position = anim.startPos * (1.0f - eased) + anim.targetPos * eased;
                    }

                    // Добавляем легкое подпрыгивание
                    if (anim.bounceHeight > 0.0f) {
                        // Параболический прыжок: 4 * t * (1 - t) даёт пик в середине
                        float bounceFactor = 4.0f * t * (1.0f - t);
                        QVector3D up = transform->position.normalized();
                        transform->position += up * bounceFactor * anim.bounceHeight;
                    }
                    break;
                }
                case Animation::Type::Rotate: {
                    // Можно добавить позже при необходимости
                    break;
                }
                case Animation::Type::Scale: {
                    // Можно добавить позже при необходимости
                    break;
                }
                default:
                    break;
                }
            }
        }

        // Удаляем завершённые анимации
        for (auto id : toRemove) {
            animations_.erase(id);
        }
    }

} // namespace ecs