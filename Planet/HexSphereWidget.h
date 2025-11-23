// HexSphereWidget.h - переработанная версия с отдельными классами
#pragma once

#include <QOpenGLWidget>
#include <QTimer>
#include <optional>

#include "InputController.h"

class CameraController;
struct TerrainParams;

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

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

    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void applyResponse(const InputController::Response& response);

    CameraController& cameraController_;
    InputController& inputController_;

    QTimer* waterTimer_ = nullptr;
};
