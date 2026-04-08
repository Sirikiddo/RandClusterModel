#pragma once

#include <QMatrix4x4>
#include <QPoint>
#include <QVector3D>
#include <memory>
#include <optional>
#include <vector>

#include "controllers/HexSphereSceneController.h"
#include "dag/TerrainBackendContract.h"
#include "renderers/HexSphereRenderer.h"
#include "ui/PerformanceStats.h"
#include "ECS/ComponentStorage.h"

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QOpenGLWidget;

class CameraController;
class EngineFacade;
class HexSphereModel;

class InputController : public ITerrainSceneBridge {
public:
    struct Response {
        bool requestUpdate = false;
        std::optional<QString> hudMessage{};
    };

    explicit InputController(CameraController& camera);
    ~InputController();

    void attachEngine(EngineFacade* engine) { engine_ = engine; }
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
    Response setSmoothOneStep(bool on);
    Response setStripInset(float v);
    Response setOutlineBias(float v);

    Response advanceWaterTime(float dt);

    Response toggleOreVisualization();
    void setOreAnimationTime(float time);
    void setOreVisualizationEnabled(bool enabled);
    float getOreAnimationTime() const;
    bool isOreVisualizationEnabled() const;
    HexSphereModel* getModel();

    Response setOreAnimationSpeed(float speed);
    Response regenerateOreDeposits();

    bool applyAnimation(int entityId, int targetCell, float speed = 1.0f, float bounceHeight = 0.05f);
    void updateAnimations(float dt);
    ecs::ComponentStorage& getECS() { return ecs_; }
    const ecs::ComponentStorage& getECS() const { return ecs_; }

    void stageTerrainParams(const TerrainParams& params) override;
    void stageGeneratorByIndex(int idx) override;
    void stageSubdivisionLevel(int level) override;
    void rebuildTerrainFromInputs() override;
    TerrainSnapshot captureTerrainSnapshot() const override;
    void projectTerrainSnapshot(const TerrainSnapshot& snapshot) override;

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
    void buildAndShowPathBetween(int startCell, int targetCell, Response& response);
    void clearPath(Response& response);
    void updateBufferUsageStrategy(int subdivisionLevel);

    std::optional<int> pickCellAt(int sx, int sy) const;
    std::optional<PickHit> pickTerrainAt(int sx, int sy) const;
    std::optional<PickHit> pickEntityAt(int sx, int sy) const;
    std::optional<PickHit> pickSceneAt(int sx, int sy) const;

    void selectEntity(int entityId, Response& response);
    void deselectEntity();
    void moveSelectedEntityToCell(int cellId, Response& response);

    CameraController& camera_;
    EngineFacade* engine_ = nullptr;
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

    float oreAnimationTime_ = 0.0f;
    bool oreVisualizationEnabled_ = true;
    float oreAnimationSpeed_ = 0.1f;
};
