// HexSphereWidget.h - переработанная версия с отдельными классами
#pragma once

#include <QOpenGLWidget>
#include <QTimer>
#include <memory>

#include "core/AppViewConfig.h"
#include "controllers/InputController.h"
#include "model/OreSystem.h"

class CameraController;
struct TerrainParams;

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QLabel;
class QFrame;
class QToolButton;
class QResizeEvent;
class QVariantAnimation;

class EngineFacade;

class HexSphereWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    HexSphereWidget(const AppViewConfig& viewConfig,
        CameraController& cameraController,
        InputController& inputController,
        QWidget* parent = nullptr);
    ~HexSphereWidget() override;

public slots:
    void setSubdivisionLevel(int L);
    void resetView();
    void clearSelection();
    void setTerrainParams(const TerrainParams& p);
    void setGeneratorByIndex(int idx);
    void regenerateTerrain();

    void setSmoothOneStep(bool on);
    void setStripInset(float v);
    void setOutlineBias(float v);
    void triggerCommand(SceneCommand command);

signals:
    void hudTextChanged(const QString&);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void resizeEvent(QResizeEvent* e) override;
    void paintGL() override;
    void paintEvent(QPaintEvent* e) override;

    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void applyResponse(const InputController::Response& response);
    void initOreSystem();
    void updateOreAnimation(float deltaTime);
    void updateOverlayLayout();
    void syncPlacementPanelState();
    void togglePlacementSelection(InputController::PlacementModel model);
    void setPlacementPanelExpanded(bool expanded, bool animated);

    AppViewConfig viewConfig_{};
    CameraController& cameraController_;
    InputController& inputController_;

    std::unique_ptr<OreSystem> oreSystem_;
    float oreAnimationTime_ = 0.0f;
    bool oreVisualizationEnabled_ = true;

    std::unique_ptr<EngineFacade> engine_;
    QElapsedTimer frameTimer_;
    bool timerStarted_ = false;

    QString overlayText_;
    QLabel* hudLabel_ = nullptr;
    QFrame* placementPanel_ = nullptr;
    QFrame* placementContent_ = nullptr;
    QToolButton* placementToggleButton_ = nullptr;
    QToolButton* factoryButton_ = nullptr;
    QToolButton* mineButton_ = nullptr;
    QToolButton* deleteButton_ = nullptr;
    QVariantAnimation* placementPanelWidthAnimation_ = nullptr;
    QVariantAnimation* placementContentWidthAnimation_ = nullptr;
    int placementPanelCollapsedWidth_ = 0;
    int placementPanelExpandedWidth_ = 0;
    int placementContentExpandedWidth_ = 0;
    bool placementPanelExpanded_ = true;
    QTimer* waterTimer_ = nullptr;

    QTimer* animationTimer_ = nullptr;
};
