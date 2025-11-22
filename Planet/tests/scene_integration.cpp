#include <QtTest/QtTest>
#include "../scene/SceneGraph.h"

class SceneIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void sceneLifecycle();
};

void SceneIntegrationTest::sceneLifecycle() {
    scene::SceneGraph graph;
    scene::CoordinateFrame root;
    auto planet = std::make_shared<scene::Entity>("Planet", "hexSphere");
    planet->transform().scale = {2.0f, 2.0f, 2.0f};

    auto handle = graph.spawn(planet);
    QVERIFY(handle->id() >= 0);

    auto rover = graph.spawn(scene::Entity("Rover", "pyramid"));
    rover->transform().position = scene::localToWorldPoint(rover->transform(), root, {1.0f, 0.0f, 0.0f});
    rover->setSelected(true);

    bool updated = false;
    graph.onUpdate([&](scene::Entity& e) {
        if (e.id() == rover->id()) updated = true;
    });

    graph.update(0.016f);
    QVERIFY(updated);

    QVERIFY(graph.selected());
    graph.destroy(rover->id());
    QVERIFY(!graph.selected());
}

QTEST_MAIN(SceneIntegrationTest)
#include "scene_integration.moc"
