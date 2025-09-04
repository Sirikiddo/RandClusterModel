#pragma once
#include <QMainWindow>
class QSpinBox; class QAction; class QLabel;
class HexSphereWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
private:
    HexSphereWidget* glw_ = nullptr;
    QSpinBox* levelSpin_ = nullptr;
    QLabel* infoLbl_ = nullptr;
};