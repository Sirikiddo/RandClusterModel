#include "HexSphereWidget.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtDebug>

#include "CameraController.h"
#include "InputController.h"

HexSphereWidget::HexSphereWidget(CameraController& cameraController, InputController& inputController, QWidget* parent)
    : QOpenGLWidget(parent)
    , cameraController_(cameraController)
    , inputController_(inputController) {
    setFocusPolicy(Qt::StrongFocus);

    auto* hud = new QLabel(this);
    hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    hud->setStyleSheet("QLabel { background: rgba(0,0,0,140); color: white; padding: 6px; }");
    hud->move(10, 10);
    hud->setText("LMB: select | C: clear path | P: build path | +/-: height | 1-8: biomes | S: smooth | W: move");
    hud->adjustSize();

    waterTimer_ = new QTimer(this);
    connect(waterTimer_, &QTimer::timeout, this, [this]() {
        applyResponse(inputController_.advanceWaterTime(0.016f));
    });
}

HexSphereWidget::~HexSphereWidget() = default;

void HexSphereWidget::initializeGL() {
    inputController_.initialize(this);

    waterTimer_->start(16);

    emit hudTextChanged("Controls: [LMB] select | [C] clear path | [P] path between selected | [+/-] height | [1-8] biomes | [S] smooth toggle | [W] move entity");
}

void HexSphereWidget::resizeGL(int w, int h) {
    inputController_.resize(w, h, devicePixelRatioF());
}

void HexSphereWidget::paintGL() {
    applyResponse(inputController_.render());
}

void HexSphereWidget::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    applyResponse(inputController_.mousePress(e));
}

void HexSphereWidget::mouseMoveEvent(QMouseEvent* e) {
    applyResponse(inputController_.mouseMove(e));
}

void HexSphereWidget::mouseReleaseEvent(QMouseEvent* e) {
    inputController_.mouseRelease(e);
}

void HexSphereWidget::wheelEvent(QWheelEvent* e) {
    applyResponse(inputController_.wheel(e));
}

void HexSphereWidget::keyPressEvent(QKeyEvent* e) {
    applyResponse(inputController_.keyPress(e));
}

void HexSphereWidget::setSubdivisionLevel(int L) {
    applyResponse(inputController_.setSubdivisionLevel(L));
}

void HexSphereWidget::resetView() {
    applyResponse(inputController_.resetView());
}

void HexSphereWidget::clearSelection() {
    applyResponse(inputController_.clearSelection());
}

void HexSphereWidget::setTerrainParams(const TerrainParams& p) {
    applyResponse(inputController_.setTerrainParams(p));
}

void HexSphereWidget::setGeneratorByIndex(int idx) {
    applyResponse(inputController_.setGeneratorByIndex(idx));
}

void HexSphereWidget::regenerateTerrain() {
    applyResponse(inputController_.regenerateTerrain());
}

void HexSphereWidget::setSmoothOneStep(bool on) {
    applyResponse(inputController_.setSmoothOneStep(on));
}

void HexSphereWidget::setStripInset(float v) {
    applyResponse(inputController_.setStripInset(v));
}

void HexSphereWidget::setOutlineBias(float v) {
    applyResponse(inputController_.setOutlineBias(v));
}

void HexSphereWidget::applyResponse(const InputController::Response& response) {
    if (response.hudMessage) {
        emit hudTextChanged(*response.hudMessage);
    }
    if (response.requestUpdate) {
        update();
    }
}
