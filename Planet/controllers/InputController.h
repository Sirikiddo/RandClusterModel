#pragma once

#include <QMatrix4x4>
#include <QPoint>
#include <QVector3D>
#include <optional>
#include <memory>

#include "controllers/HexSphereSceneController.h"
#include "renderers/HexSphereRenderer.h"
#include "ui/PerformanceStats.h"
#include "ECS/ComponentStorage.h"

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QOpenGLWidget;

class CameraController;
class HexSphereModel;

class InputController {
public:
    struct Response {
        bool requestUpdate = false;
        std::optional<QString> hudMessage{};
    };

    explicit InputController(CameraController& camera);

    void initialize(QOpenGLWidget* owner);
    void resize(int w, int h, float devicePixelRatio);
    Response render();

    Response mousePress(QMouseEvent* e);
    Response mouseMove(QMouseEvent* e);
    void mouseRelease(QMouseEvent* e);
    Response wheel(QWheelEvent* e);
    Response keyPress(QKeyEvent* e);

    Response setSubdivisionLevel(int L);
    Response resetView();
    Response clearSelection();
    Response setTerrainParams(const TerrainParams& p);
    Response setGeneratorByIndex(int idx);
    Response regenerateTerrain();
    Response toggleCellSelection(int cellId);
    Response setSmoothOneStep(bool on);
    Response setStripInset(float v);
    Response setOutlineBias(float v);

    Response advanceWaterTime(float dt);

    // Äëÿ ñèñòåìû ðóä - íîâûå ìåòîäû
    Response toggleOreVisualization();
    void setOreAnimationTime(float time);
    void setOreVisualizationEnabled(bool enabled);
    float getOreAnimationTime() const;
    bool isOreVisualizationEnabled() const;
    HexSphereModel* getModel();

    // Äîïîëíèòåëüíûå ìåòîäû äëÿ óïðàâëåíèÿ ñèñòåìîé ðóä
    Response setOreAnimationSpeed(float speed);
    Response regenerateOreDeposits();

private:
    struct PickHit {
        int cellId;
        int entityId;
        QVector3D pos;
        float t;
        bool isEntity;
    };

    void rebuildModel(Response& response);
    void uploadSelection();
    void uploadBuffers();
    void buildAndShowSelectedPath(Response& response);
    void clearPath(Response& response);
    void updateBufferUsageStrategy();

    std::optional<int> pickCellAt(int sx, int sy) const;
    std::optional<PickHit> pickTerrainAt(int sx, int sy) const;
    std::optional<PickHit> pickEntityAt(int sx, int sy) const;
    std::optional<PickHit> pickSceneAt(int sx, int sy) const;

    void selectEntity(int entityId, Response& response);
    void deselectEntity();
    void moveSelectedEntityToCell(int cellId, Response& response);

    CameraController& camera_;
    QOpenGLWidget* owner_ = nullptr;
    std::unique_ptr<HexSphereRenderer> renderer_;

    HexSphereSceneController scene_{};
    ecs::ComponentStorage ecs_{};
    PerformanceStats stats_{};

    HexSphereRenderer::UploadOptions uploadOptions_{};
    int selectedEntityId_ = -1;

    QPoint lastPos_;
    bool rotating_ = false;

    float waterTime_ = 0.0f;
    QVector3D lightDir_ = QVector3D(1, 1, 1).normalized();

    // Ïàðàìåòðû äëÿ ñèñòåìû ðóä
    float oreAnimationTime_ = 0.0f;
    bool oreVisualizationEnabled_ = true;
    float oreAnimationSpeed_ = 0.1f;
};