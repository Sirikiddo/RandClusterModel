#pragma once
#include <QMainWindow>
#include <memory>
class QSpinBox; class QAction; class QLabel;
class HexSphereWidget;
class CameraController;
class InputController;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
private:
    std::unique_ptr<CameraController> cameraController_;
    std::unique_ptr<InputController> inputController_;
    HexSphereWidget* glw_ = nullptr;
    QSpinBox* levelSpin_ = nullptr;
    QLabel* infoLbl_ = nullptr;
};