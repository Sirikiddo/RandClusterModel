// HexSphereWidget.h - переработанная версия с отдельными классами
#pragma once

#include <QOpenGLWidget>
#include <QTimer>
#include <optional>
#include <memory>

#include "controllers/InputController.h"
#include "OreSystem.h"

class CameraController;
struct TerrainParams;

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

class EngineFacade;

class HexSphereWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    HexSphereWidget(CameraController& cameraController, InputController& inputController, QWidget* parent = nullptr);
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

signals:
    void hudTextChanged(const QString&);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
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

    CameraController& cameraController_;
    InputController& inputController_;

    std::unique_ptr<OreSystem> oreSystem_;
    float oreAnimationTime_ = 0.0f;
    bool oreVisualizationEnabled_ = true;

    std::unique_ptr<EngineFacade> engine_;
    QElapsedTimer frameTimer_;
    bool timerStarted_ = false;

    QString overlayText_;
    QTimer* waterTimer_ = nullptr;
};