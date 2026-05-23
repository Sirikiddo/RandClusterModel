#include "ui/MainWindow.h"
#include "core/AppViewConfig.h"
#include "controllers/CameraController.h"
#include "controllers/InputController.h"
#include "dag/DagBackendBenchmark.h"
#include "ui/HexSphereWidget.h"
#include "ui/PlanetSettingsPanel.h"
#include "generation/TerrainGenerator.h"
#include <QAction>
#include <QDockWidget>
#include <QDir>
#include <QLabel>
#include <QMenuBar>
#include <QKeySequence>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(const AppViewConfig& viewConfig, QWidget* parent) : QMainWindow(parent) {
    cameraController_ = std::make_unique<CameraController>();
    inputController_ = std::make_unique<InputController>(*cameraController_, viewConfig.sceneViewMode);

    glw_ = new HexSphereWidget(viewConfig, *cameraController_, *inputController_, this);
    setCentralWidget(glw_);

    auto* tb = addToolBar("Controls");
    levelSpin_ = new QSpinBox(tb);
    levelSpin_->setRange(0, 7);
    levelSpin_->setValue(2);
    tb->addWidget(new QLabel(" Subdivision L: "));
    tb->addWidget(levelSpin_);

    auto* resetAct = tb->addAction("Reset View");
    auto* clearSelAct = tb->addAction("Clear Selection");

    auto* commandMenu = menuBar()->addMenu("&Commands");
    auto addCommand = [&](const QString& text, const QString& shortcut, SceneCommand command, bool addToToolbar) {
        QAction* action = commandMenu->addAction(text);
        if (!shortcut.isEmpty()) {
            action->setShortcut(QKeySequence(shortcut));
            action->setShortcutContext(Qt::ApplicationShortcut);
        }
        action->setEnabled(!viewConfig.isContributorMode());
        connect(action, &QAction::triggered, this, [this, command] {
            glw_->triggerCommand(command);
            });
        if (addToToolbar) {
            tb->addAction(action);
        }
        return action;
        };

    addCommand("Clear Path", "C", SceneCommand::ClearPath, false);
    addCommand("Build Path", "P", SceneCommand::BuildPath, true);
    addCommand("Move Explorer", "W", SceneCommand::MoveSelectedEntity, true);
    addCommand("Toggle Smooth", "S", SceneCommand::ToggleSmooth, true);
    addCommand("Height +", "+", SceneCommand::IncreaseHeight, true);
    addCommand("Height -", "-", SceneCommand::DecreaseHeight, true);
    addCommand("Toggle Ore", "O", SceneCommand::ToggleOreVisualization, true);
    commandMenu->addSeparator();
    addCommand("Biome 1: Sea", "1", SceneCommand::SetBiomeSea, false);
    addCommand("Biome 2: Grass", "2", SceneCommand::SetBiomeGrass, false);
    addCommand("Biome 3: Rock", "3", SceneCommand::SetBiomeRock, false);
    addCommand("Biome 4: Snow", "4", SceneCommand::SetBiomeSnow, false);
    addCommand("Biome 5: Tundra", "5", SceneCommand::SetBiomeTundra, false);
    addCommand("Biome 6: Desert", "6", SceneCommand::SetBiomeDesert, false);
    addCommand("Biome 7: Savanna", "7", SceneCommand::SetBiomeSavanna, false);
    addCommand("Biome 8: Jungle", "8", SceneCommand::SetBiomeJungle, false);
    commandMenu->addSeparator();
    QAction* benchmarkAct = commandMenu->addAction("Run DAG Benchmark");
    connect(benchmarkAct, &QAction::triggered, this, [this] {
        const QString csvPath = QDir::current().filePath("dag_backend_benchmark_results.csv");
        const DagBenchmarkReport report = runDagBackendBenchmark(csvPath);
        statusBar()->showMessage(
            report.ok
                ? QString("DAG benchmark saved: %1").arg(report.csvPath)
                : QString("DAG benchmark finished with compatibility/write issues: %1").arg(report.csvPath),
            8000);
        });

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

    if (viewConfig.isContributorMode()) {
        levelSpin_->setEnabled(false);
        clearSelAct->setEnabled(false);
        panel->setContributorMode(true);
    }

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
