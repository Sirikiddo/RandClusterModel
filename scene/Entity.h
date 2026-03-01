#pragma once
#include <QString>
#include <memory>
#include <vector>
#include "Transform.h"
#include "Interaction.h"

namespace scene {

class IEntity {
public:
    virtual ~IEntity() = default;
    virtual int id() const = 0;
    virtual void setId(int id) = 0;
    virtual QString name() const = 0;
    virtual QString mesh() const = 0;
    virtual Transform& transform() = 0;
    virtual const Transform& transform() const = 0;
    virtual void update(float dt) = 0;
};

class Entity : public IEntity {
public:
    Entity(const QString& name = QString(), const QString& meshId = QString("pyramid"));
    Entity(const Entity& other);
    Entity& operator=(const Entity& other);
    Entity(Entity&&) noexcept = default;
    Entity& operator=(Entity&&) noexcept = default;

    int id() const override { return id_; }
    void setId(int id) override { id_ = id; }

    QString name() const override { return name_; }
    void setName(const QString& name) { name_ = name; }

    QString mesh() const override { return meshId_; }
    void setMesh(const QString& meshId) { meshId_ = meshId; }

    Transform& transform() override { return transform_; }
    const Transform& transform() const override { return transform_; }

    int currentCell() const { return currentCell_; }
    void setCurrentCell(int cellId) { currentCell_ = cellId; }

    bool selected() const { return selected_; }
    void setSelected(bool flag) { selected_ = flag; }

    void attachCollider(std::unique_ptr<ICollider> collider) { collider_ = std::move(collider); }
    ICollider* collider() const { return collider_.get(); }

    void attachLightReceiver(std::unique_ptr<ILightReceiver> receiver) { lightReceiver_ = std::move(receiver); }
    ILightReceiver* lightReceiver() const { return lightReceiver_.get(); }

    void attachPhysicalBody(std::unique_ptr<IPhysicalBody> body) { body_ = std::move(body); }
    IPhysicalBody* physicalBody() const { return body_.get(); }

    void update(float dt) override;

private:
    int id_ = -1;
    QString name_;
    QString meshId_ = "pyramid";
    Transform transform_{};
    int currentCell_ = -1;
    bool selected_ = false;
    std::unique_ptr<ICollider> collider_;
    std::unique_ptr<ILightReceiver> lightReceiver_;
    std::unique_ptr<IPhysicalBody> body_;
};

using EntityHandle = int;

} // namespace scene

