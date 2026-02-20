#include "ui/HexSphereWidget.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtDebug>
#include <QElapsedTimer>
#include <QPainter>
#include <QPaintEvent>
#include <memory>

#include "core/EngineFacade.h"

#include "controllers/CameraController.h"
#include "controllers/InputController.h"

#include "OreSystem.h"

HexSphereWidget::HexSphereWidget(CameraController& cameraController, InputController& inputController, QWidget* parent)
    : QOpenGLWidget(parent)
    , cameraController_(cameraController)
    , inputController_(inputController)
    , oreSystem_(nullptr) {
    setFocusPolicy(Qt::StrongFocus);

    auto* hud = new QLabel(this);
    hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    hud->setStyleSheet("QLabel { background: rgba(0,0,0,140); color: white; padding: 6px; }");
    hud->move(10, 10);
    hud->setText("LMB: select | C: clear path | P: build path | +/-: height | 1-8: biomes | S: smooth | W: move | O: ore viz");
    hud->adjustSize();

    waterTimer_ = new QTimer(this);
    connect(waterTimer_, &QTimer::timeout, this, [this]() {
        applyResponse(inputController_.advanceWaterTime(0.016f));

        // ќбновл€ем анимацию руды
        inputController_.setOreAnimationTime(inputController_.getOreAnimationTime() + 0.016f * 0.1f);
        update(); // «апрашиваем перерисовку
        });

    engine_ = std::make_unique<EngineFacade>(inputController_);
}

HexSphereWidget::~HexSphereWidget() = default;

void HexSphereWidget::initializeGL() {
    inputController_.initialize(this);

    // »нициализируем систему руд после загрузки модели
    // ќтложим до первого рендера или создани€ мира
    // initOreSystem() будет вызван после генерации мира

    waterTimer_->start(16);

    emit hudTextChanged("Controls: [LMB] select | [C] clear path | [P] path between selected | [+/-] height | [1-8] biomes | [S] smooth toggle | [W] move entity | [O] toggle ore visualization");
}

void HexSphereWidget::resizeGL(int w, int h) {
    inputController_.resize(w, h, devicePixelRatioF());
}

void HexSphereWidget::paintGL() {
    //applyResponse(inputController_.render());
    if (!context() || !context()->isValid() || !isValid()) return;

    // dt
    if (!timerStarted_) {
        frameTimer_.start();
        timerStarted_ = true;
    }
    const qint64 ns = frameTimer_.nsecsElapsed();
    frameTimer_.restart();
    float dt = float(ns) * 1e-9f;

    // 1) tick facade (пока только overlay/fps)
    engine_->tick(dt);

    // 2) старый рендер как есть
    inputController_.render(); // или как у теб€ называетс€

    // 3) рисуем текст поверх (после GL)
    const auto& o = engine_->overlay();
    overlayText_ = QString("v:%1  dirty:%2  busy:%3  dt:%4ms  fps:%5")
        .arg(qulonglong(o.sceneVersion))
        .arg(o.hasPlan ? "1" : "0")
        .arg(o.asyncBusy ? "1" : "0")
        .arg(QString::number(o.dtMs, 'f', 2))
        .arg(QString::number(o.fps, 'f', 1));
}

void HexSphereWidget::paintEvent(QPaintEvent* e) {
    // 1) сначала пусть QOpenGLWidget нормально нарисует GL кадр
    QOpenGLWidget::paintEvent(e);

    // 2) теперь поверх кадра (уже в 2D) рисуем текст
    if (overlayText_.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.drawText(10, 20, overlayText_);
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

    // ѕосле регенерации мира переинициализируем систему руд
    // Ќужно подождать, пока мир будет сгенерирован
    QTimer::singleShot(100, this, [this]() {
        initOreSystem();
        });
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

void HexSphereWidget::initOreSystem() {
    // —оздаем систему руд
    oreSystem_ = std::make_unique<OreSystem>();

    // ѕолучаем модель через InputController
    HexSphereModel* model = inputController_.getModel();
    if (model) {
        oreSystem_->initialize(*model);
        qDebug() << "OreSystem initialized with" << oreSystem_->getDepositCount() << "deposits";
    }
    else {
        qDebug() << "Failed to initialize OreSystem: model is null";
    }

    oreAnimationTime_ = 0.0f;
    oreVisualizationEnabled_ = true;

    // ќбновл€ем тессел€тор с временем анимации
    inputController_.setOreAnimationTime(oreAnimationTime_);
    inputController_.setOreVisualizationEnabled(oreVisualizationEnabled_);
}

void HexSphereWidget::updateOreAnimation(float deltaTime) {
    oreAnimationTime_ += deltaTime * 0.1f; // —корость анимации

    // ќбновл€ем врем€ анимации в InputController
    inputController_.setOreAnimationTime(oreAnimationTime_);
}