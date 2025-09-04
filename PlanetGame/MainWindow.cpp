#include "MainWindow.h"
#include "HexSphereWidget.h"
#include <QSpinBox>
#include <QToolBar>
#include <QLabel>
#include <QAction>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    glw_ = new HexSphereWidget(this);
    setCentralWidget(glw_);

    auto* tb = addToolBar("Controls");
    levelSpin_ = new QSpinBox(tb);
    levelSpin_->setRange(0, 7); // conservative upper bound; can raise
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
}
