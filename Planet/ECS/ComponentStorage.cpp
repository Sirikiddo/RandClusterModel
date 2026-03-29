#include "ComponentStorage.h"
#include "Animation.h"

#include <algorithm>
#include <cmath>

namespace ecs {

    static QVector3D samplePolylineByDistance(const Animation& anim, float distance) {
        if (anim.pathPoints.empty()) return anim.startPos;
        if (anim.pathPoints.size() == 1) return anim.pathPoints.front();
        if (anim.pathTotalLength <= 1e-6f) return anim.pathPoints.back();

        distance = std::clamp(distance, 0.0f, anim.pathTotalLength);
        const auto& cum = anim.pathCumulative;
        const auto& pts = anim.pathPoints;
        if (cum.size() != pts.size() || cum.size() < 2) {
            return pts.back();
        }

        size_t i = 1;
        while (i < cum.size() && cum[i] < distance) ++i;
        if (i >= cum.size()) return pts.back();

        const float d0 = cum[i - 1];
        const float d1 = cum[i];
        const float denom = (d1 - d0);
        const float localT = (denom > 1e-6f) ? ((distance - d0) / denom) : 0.0f;
        return pts[i - 1] * (1.0f - localT) + pts[i] * localT;
    }

    static float arcHeightFactor(const Animation& anim, float t) {
        const float peakT = std::clamp(anim.arcPeakT, 0.05f, 0.95f);
        if (!anim.softLanding && std::abs(peakT - 0.5f) < 1e-3f) {
            return 4.0f * t * (1.0f - t);
        }

        if (t <= peakT) {
            return Animation::easeOutCubic(t / peakT);
        }

        const float localT = (t - peakT) / (1.0f - peakT);
        if (anim.softLanding) {
            return 1.0f - Animation::easeInOutQuad(localT);
        }
        return 1.0f - localT;
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
        animations_.erase(id);
        entityOrder_.erase(std::remove(entityOrder_.begin(), entityOrder_.end(), id), entityOrder_.end());
    }

    void ComponentStorage::clear() {
        entities_.clear();
        transforms_.clear();
        meshes_.clear();
        colliders_.clear();
        materials_.clear();
        scripts_.clear();
        animations_.clear();
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
        for (auto& [id, script] : scripts_) {
            if (script.onUpdate) {
                script.onUpdate(id, dt);
            }
        }

        std::vector<EntityId> toRemove;
        for (auto& [id, anim] : animations_) {
            anim.elapsed += dt;

            if (anim.isFinished()) {
                if (anim.onComplete) {
                    anim.onComplete(id);
                }
                toRemove.push_back(id);
                continue;
            }

            const float t = anim.progress();
            if (auto* transform = get<Transform>(id)) {
                switch (anim.type) {
                case Animation::Type::MoveTo: {
                    if (!anim.pathPoints.empty()) {
                        const float d = t * anim.pathTotalLength;
                        transform->position = samplePolylineByDistance(anim, d);
                    }
                    else {
                        transform->position = anim.startPos * (1.0f - t) + anim.targetPos * t;
                    }

                    if (anim.bounceHeight > 0.0f) {
                        const QVector3D up = transform->position.normalized();
                        transform->position += up * arcHeightFactor(anim, t) * anim.bounceHeight;
                    }
                    break;
                }
                case Animation::Type::Rotate:
                case Animation::Type::Scale:
                default:
                    break;
                }
            }
        }

        for (auto id : toRemove) {
            animations_.erase(id);
        }
    }

} // namespace ecs
