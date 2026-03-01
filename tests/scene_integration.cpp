#include <QtTest/QtTest>
#include "../ECS/ComponentStorage.h"

class SceneIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void sceneLifecycle();
};

void SceneIntegrationTest::sceneLifecycle() {
    ecs::ComponentStorage ecs;
    ecs::CoordinateFrame root;

    auto& planet = ecs.createEntity("Planet");
    planet.currentCell = 0;
    ecs.emplace<ecs::Mesh>(planet.id).meshId = "hexSphere";
    ecs::Transform& planetTransform = ecs.emplace<ecs::Transform>(planet.id);
    planetTransform.scale = {2.0f, 2.0f, 2.0f};

    auto& rover = ecs.createEntity("Rover");
    ecs.emplace<ecs::Mesh>(rover.id).meshId = "pyramid";
    ecs::Transform& roverTransform = ecs.emplace<ecs::Transform>(rover.id);
    roverTransform.position = ecs::localToWorldPoint(roverTransform, root, {1.0f, 0.0f, 0.0f});
    ecs.setSelected(rover.id, true);

    bool updated = false;
    auto& roverScript = ecs.emplace<ecs::Script>(rover.id);
    roverScript.onUpdate = [&](ecs::EntityId id, float) {
        if (id == rover.id) updated = true;
    };

    ecs.update(0.016f);
    QVERIFY(updated);

    QVERIFY(ecs.selectedEntity());
    ecs.destroyEntity(rover.id);
    QVERIFY(!ecs.selectedEntity());
}

QTEST_MAIN(SceneIntegrationTest)
#include "scene_integration.moc"
