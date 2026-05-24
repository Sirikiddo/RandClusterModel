#include "ui/HexSphereWidget.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QVariantAnimation>
#include <QIcon>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QSize>
#include <memory>

#include "dag/EngineFacade.h"

#include "controllers/CameraController.h"
#include "controllers/InputController.h"

#include "model/OreSystem.h"

HexSphereWidget::HexSphereWidget(const AppViewConfig& viewConfig,
    CameraController& cameraController,
    InputController& inputController,
    QWidget* parent)
    : QOpenGLWidget(parent)
    , viewConfig_(viewConfig)
    , cameraController_(cameraController)
    , inputController_(inputController)
    , engine_(viewConfig.isContributorMode() ? nullptr : std::make_unique<EngineFacade>())
    , oreSystem_(nullptr) {

    if (engine_) {
        inputController_.attachEngine(engine_.get());
        engine_->attachTerrainBridge(&inputController_);
    }

    setFocusPolicy(Qt::StrongFocus);

    hudLabel_ = new QLabel(this);
    hudLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hudLabel_->setWordWrap(true);
    hudLabel_->setMaximumWidth(420);
    hudLabel_->setStyleSheet("QLabel { background: rgba(0,0,0,140); color: white; padding: 6px; border-radius: 8px; }");
    if (viewConfig_.isContributorMode()) {
        hudLabel_->setText("Contributor mode: RMB drag rotate | wheel zoom | Reset View");
    }
    else {
        hudLabel_->setText("LMB: select | C: clear path | P: build path | +/-: height | 1-8: biomes | S: smooth | W: move | O: ore viz");
    }

    if (!viewConfig_.isContributorMode()) {
        placementPanel_ = new QFrame(this);
        placementPanel_->setStyleSheet("QFrame { background: rgba(18, 23, 31, 210); border: 1px solid rgba(255,255,255,40); border-radius: 12px; }");

        auto* panelLayout = new QHBoxLayout(placementPanel_);
        panelLayout->setContentsMargins(10, 10, 10, 14);
        panelLayout->setSpacing(8);

        placementToggleButton_ = new QToolButton(placementPanel_);
        placementToggleButton_->setText("Build\n<");
        placementToggleButton_->setCursor(Qt::PointingHandCursor);
        placementToggleButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        placementToggleButton_->setFixedSize(64, 118);
        placementToggleButton_->setStyleSheet(
            "QToolButton {"
            " color: white;"
            " font-weight: 600;"
            " padding: 2px 4px;"
            " border: none;"
            " border-radius: 8px;"
            " background: rgba(255,255,255,10);"
            " text-align: center;"
            "}"
            "QToolButton:hover { background: rgba(255,255,255,18); }");
        panelLayout->addWidget(placementToggleButton_, 0, Qt::AlignTop);

        placementContent_ = new QFrame(placementPanel_);
        placementContent_->setStyleSheet("QFrame { background: transparent; border: none; }");
        auto* contentLayout = new QHBoxLayout(placementContent_);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(8);

        auto configurePlacementButton = [](QToolButton* button, const QString& text, const QString& imagePath) {
            button->setText(text);
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(72, 72));
            button->setFixedSize(96, 118);
            button->setStyleSheet(
                "QToolButton {"
                " color: white;"
                " background: rgba(255,255,255,18);"
                " border: 1px solid rgba(255,255,255,32);"
                " border-radius: 10px;"
                " padding: 6px;"
                "}"
                "QToolButton:hover { background: rgba(255,255,255,28); }"
                "QToolButton:checked { background: rgba(74, 163, 255, 110); border: 1px solid rgba(127,197,255,180); }");

            const QPixmap preview(imagePath);
            if (!preview.isNull()) {
                button->setIcon(QIcon(preview));
            }
        };

        factoryButton_ = new QToolButton(placementContent_);
        mineButton_ = new QToolButton(placementContent_);
        deleteButton_ = new QToolButton(placementContent_);
        configurePlacementButton(factoryButton_, "Factory", "resources/models/factory.jpg");
        configurePlacementButton(mineButton_, "Mine", "resources/models/mine.jpg");
        deleteButton_->setText("Delete");
        deleteButton_->setCheckable(true);
        deleteButton_->setCursor(Qt::PointingHandCursor);
        deleteButton_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        deleteButton_->setIconSize(QSize(72, 72));
        deleteButton_->setFixedSize(96, 118);
        deleteButton_->setStyleSheet(
            "QToolButton {"
            " color: white;"
            " background: rgba(255,120,120,20);"
            " border: 1px solid rgba(255,160,160,55);"
            " border-radius: 10px;"
            " padding: 6px;"
            "}"
            "QToolButton:hover { background: rgba(255,120,120,35); }"
            "QToolButton:checked { background: rgba(220,70,70,130); border: 1px solid rgba(255,180,180,180); }");

        contentLayout->addWidget(factoryButton_);
        contentLayout->addWidget(mineButton_);
        contentLayout->addWidget(deleteButton_);
        panelLayout->addWidget(placementContent_, 0, Qt::AlignLeft | Qt::AlignTop);

        placementPanelWidthAnimation_ = new QVariantAnimation(this);
        placementPanelWidthAnimation_->setDuration(220);
        placementPanelWidthAnimation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(placementPanelWidthAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            if (!placementPanel_) {
                return;
            }
            placementPanel_->setFixedWidth(value.toInt());
            updateOverlayLayout();
        });

        placementContentWidthAnimation_ = new QVariantAnimation(this);
        placementContentWidthAnimation_->setDuration(220);
        placementContentWidthAnimation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(placementContentWidthAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            if (!placementContent_) {
                return;
            }
            placementContent_->setFixedWidth(value.toInt());
        });

        connect(placementToggleButton_, &QToolButton::clicked, this, [this]() {
            setPlacementPanelExpanded(!placementPanelExpanded_, true);
        });
        connect(factoryButton_, &QToolButton::clicked, this, [this]() {
            togglePlacementSelection(InputController::PlacementModel::Factory);
        });
        connect(mineButton_, &QToolButton::clicked, this, [this]() {
            togglePlacementSelection(InputController::PlacementModel::Mine);
        });
        connect(deleteButton_, &QToolButton::clicked, this, [this]() {
            togglePlacementSelection(InputController::PlacementModel::Delete);
        });

        placementContent_->adjustSize();
        placementContentExpandedWidth_ = placementContent_->sizeHint().width();
        placementPanelCollapsedWidth_ = 10 + placementToggleButton_->width() + 10;
        placementPanelExpandedWidth_ = placementPanelCollapsedWidth_ + 8 + placementContentExpandedWidth_;
        placementPanel_->setFixedHeight(10 + placementToggleButton_->height() + 14);
        placementContent_->setFixedWidth(placementContentExpandedWidth_);
        setPlacementPanelExpanded(true, false);
        syncPlacementPanelState();
    }

    updateOverlayLayout();

    waterTimer_ = new QTimer(this);

    connect(waterTimer_, &QTimer::timeout, this, [this]() {
        applyResponse(inputController_.advanceWaterTime(0.016f));
        inputController_.setOreAnimationTime(inputController_.getOreAnimationTime() + 0.016f * 0.1f);
        update();
        });
}

HexSphereWidget::~HexSphereWidget() = default;

void HexSphereWidget::initializeGL() {
    inputController_.initialize(this);
    if (engine_) {
        engine_->initializeTerrainState();
    }

    animationTimer_ = new QTimer(this);
    connect(animationTimer_, &QTimer::timeout, this, [this]() {
        static QElapsedTimer timer;
        if (!timer.isValid()) {
            timer.start();
            return;
        }
        float dt = timer.restart() / 1000.0f;
        dt = std::min(dt, 0.033f);

        inputController_.updateAnimations(dt);
        update();
        });
    animationTimer_->start(16);

    if (viewConfig_.isContributorMode()) {
        emit hudTextChanged("Contributor mode: camera only");
    }
    else {
        emit hudTextChanged("Controls: [LMB] select | [C] clear path | [P] path between selected | [+/-] height | [1-8] biomes | [S] smooth toggle | [W] move entity | [O] toggle ore visualization");
        syncPlacementPanelState();
    }
    updateOverlayLayout();
}

void HexSphereWidget::resizeGL(int w, int h) {
    inputController_.resize(w, h, devicePixelRatioF());
}

void HexSphereWidget::resizeEvent(QResizeEvent* e) {
    QOpenGLWidget::resizeEvent(e);
    updateOverlayLayout();
}


void HexSphereWidget::paintGL() {
    if (!context() || !context()->isValid() || !isValid()) return;

    if (!timerStarted_) {
        frameTimer_.start();
        timerStarted_ = true;
    }
    const qint64 ns = frameTimer_.nsecsElapsed();
    frameTimer_.restart();
    float dt = float(ns) * 1e-9f;
    dt = std::min(dt, 0.1f);

    // ������� ��� - ������ ���������� � �������
    // inputController_.getECS().update(dt);

    if (engine_) {
        engine_->tick(dt);
    }
    inputController_.render();
    if (engine_) {
        const auto& o = engine_->overlay();
        const auto& sceneDag = engine_->lastSceneDagStats();
        overlayText_ = QString("v:%1  dirty:%2  busy:%3  dt:%4ms  fps:%5  dag exec:%6 skip:%7 cache:%8")
            .arg(qulonglong(o.sceneVersion))
            .arg(o.hasPlan ? "1" : "0")
            .arg(o.asyncBusy ? "1" : "0")
            .arg(QString::number(o.dtMs, 'f', 2))
            .arg(QString::number(o.fps, 'f', 1))
            .arg(sceneDag.executedNodes)
            .arg(sceneDag.skippedGuardNodes)
            .arg(sceneDag.cacheHits);
    }
    else {
        overlayText_ = QString("contributor:1  dt:%1ms").arg(QString::number(dt * 1000.0f, 'f', 2));
    }
}

void HexSphereWidget::paintEvent(QPaintEvent* e) {
    // 1) ������� ����� QOpenGLWidget ��������� �������� GL ����
    QOpenGLWidget::paintEvent(e);

    // 2) ������ ������ ����� (��� � 2D) ������ �����
    if (overlayText_.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setPen(Qt::white);
    p.drawText(10, height() - 12, overlayText_);
}

void HexSphereWidget::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    applyResponse(inputController_.mousePress(e));
    syncPlacementPanelState();
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

    // ����� ����������� ���� ������������������ ������� ���
    // ����� ���������, ���� ��� ����� ������������
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

void HexSphereWidget::triggerCommand(SceneCommand command) {
    applyResponse(inputController_.executeCommand(command));
}

void HexSphereWidget::applyResponse(const InputController::Response& response) {
    if (response.hudMessage) {
        if (hudLabel_) {
            hudLabel_->setText(*response.hudMessage);
            hudLabel_->adjustSize();
            updateOverlayLayout();
        }
        emit hudTextChanged(*response.hudMessage);
    }
    if (response.requestUpdate) {
        update();
    }
}

void HexSphereWidget::updateOverlayLayout() {
    constexpr int margin = 10;
    constexpr int gap = 8;

    if (placementPanel_) {
        placementPanel_->move(margin, margin);
    }

    if (hudLabel_) {
        hudLabel_->adjustSize();
        const int hudY = placementPanel_
            ? placementPanel_->y() + placementPanel_->height() + gap
            : margin;
        hudLabel_->move(margin, hudY);
    }
}

void HexSphereWidget::syncPlacementPanelState() {
    if (!placementToggleButton_ || !factoryButton_ || !mineButton_ || !deleteButton_) {
        return;
    }

    const auto activeModel = inputController_.placementModel();
    factoryButton_->setChecked(activeModel == InputController::PlacementModel::Factory);
    mineButton_->setChecked(activeModel == InputController::PlacementModel::Mine);
    deleteButton_->setChecked(activeModel == InputController::PlacementModel::Delete);

    placementToggleButton_->setText(QString("Build\n%1")
        .arg(placementPanelExpanded_ ? "<" : ">"));
}

void HexSphereWidget::togglePlacementSelection(InputController::PlacementModel model) {
    const auto nextModel = inputController_.placementModel() == model
        ? InputController::PlacementModel::None
        : model;
    applyResponse(inputController_.setPlacementModel(nextModel));
    syncPlacementPanelState();
}

void HexSphereWidget::setPlacementPanelExpanded(bool expanded, bool animated) {
    if (!placementContent_ || !placementPanel_) {
        return;
    }

    placementPanelExpanded_ = expanded;
    const int targetPanelWidth = expanded ? placementPanelExpandedWidth_ : placementPanelCollapsedWidth_;
    const int targetWidth = expanded ? placementContentExpandedWidth_ : 0;

    if (placementPanelWidthAnimation_) {
        placementPanelWidthAnimation_->stop();
    }
    if (placementContentWidthAnimation_) {
        placementContentWidthAnimation_->stop();
    }

    if (animated) {
        if (placementPanelWidthAnimation_) {
            placementPanelWidthAnimation_->setStartValue(placementPanel_->width());
            placementPanelWidthAnimation_->setEndValue(targetPanelWidth);
            placementPanelWidthAnimation_->start();
        }
        if (placementContentWidthAnimation_) {
            placementContentWidthAnimation_->setStartValue(placementContent_->width());
            placementContentWidthAnimation_->setEndValue(targetWidth);
            placementContentWidthAnimation_->start();
        }
    }
    else {
        placementPanel_->setFixedWidth(targetPanelWidth);
        placementContent_->setFixedWidth(targetWidth);
    }

    syncPlacementPanelState();
    updateOverlayLayout();
}

void HexSphereWidget::initOreSystem() {
    // ������� ������� ���
    oreSystem_ = std::make_unique<OreSystem>();

    // �������� ������ ����� InputController
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

    // ��������� ���������� � �������� ��������
    inputController_.setOreAnimationTime(oreAnimationTime_);
    inputController_.setOreVisualizationEnabled(oreVisualizationEnabled_);
}

void HexSphereWidget::updateOreAnimation(float deltaTime) {
    oreAnimationTime_ += deltaTime * 0.1f; // �������� ��������

    // ��������� ����� �������� � InputController
    inputController_.setOreAnimationTime(oreAnimationTime_);
}
