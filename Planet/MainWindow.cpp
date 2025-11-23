#include "MainWindow.h"
#include "CameraController.h"
#include "HexSphereWidget.h"
#include "InputController.h"
#include "PlanetSettingsPanel.h"
#include "TerrainGenerator.h"
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    cameraController_ = std::make_unique<CameraController>();
    inputController_ = std::make_unique<InputController>(*cameraController_);
    glw_ = new HexSphereWidget(*cameraController_, *inputController_, this);
    setCentralWidget(glw_);

    auto* tb = addToolBar("Controls");
    levelSpin_ = new QSpinBox(tb);
    levelSpin_->setRange(0, 7);
    levelSpin_->setValue(2);
    tb->addWidget(new QLabel(" Subdivision L: "));
    tb->addWidget(levelSpin_);

    auto* resetAct = tb->addAction("Reset View");
    auto* clearSelAct = tb->addAction("Clear Selection");

    connect(levelSpin_, qOverload<int>(&QSpinBox::valueChanged), glw_, &HexSphereWidget::setSubdivisionLevel);
    connect(resetAct, &QAction::triggered, glw_, &HexSphereWidget::resetView);
    connect(clearSelAct, &QAction::triggered, glw_, &HexSphereWidget::clearSelection);

    infoLbl_ = new QLabel(this);
    statusBar()->addPermanentWidget(infoLbl_);
    connect(glw_, &HexSphereWidget::hudTextChanged, infoLbl_, &QLabel::setText);

    glw_->setSubdivisionLevel(levelSpin_->value());

    auto* dock = new QDockWidget("Planet Settings", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new PlanetSettingsPanel(dock);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* showDockAct = dock->toggleViewAction();
    showDockAct->setText("Planet Settings");
    viewMenu->addAction(showDockAct);

    connect(panel, &PlanetSettingsPanel::generatorChanged,
        glw_, &HexSphereWidget::setGeneratorByIndex);

    connect(panel, &PlanetSettingsPanel::paramsChanged,
        glw_, &HexSphereWidget::setTerrainParams);

    connect(panel, &PlanetSettingsPanel::visualizeChanged,
        this, [this](bool smooth, double inset, double outline) {
            glw_->setSmoothOneStep(smooth);
            glw_->setStripInset(float(inset));
            glw_->setOutlineBias(float(outline));
        });

    connect(panel, &PlanetSettingsPanel::requestRegenerate,
        glw_, &HexSphereWidget::regenerateTerrain);
}

MainWindow::~MainWindow() = default;
