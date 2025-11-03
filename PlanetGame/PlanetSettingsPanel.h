#pragma once
#include <QWidget>
#include <memory>

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;

struct TerrainParams;

class PlanetSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlanetSettingsPanel(QWidget* parent = nullptr);

signals:
    void generatorChanged(int index);                // 0: NoOp, 1: Sine, 2: Perlin
    void paramsChanged(const TerrainParams& p);
    void visualizeChanged(bool smoothOneStep, double stripInset, double outlineBias);
    void requestRegenerate();

private:
    QComboBox* genBox_ = nullptr;
    QSpinBox* seedBox_ = nullptr;
    QSpinBox* seaBox_ = nullptr;
    QDoubleSpinBox* scaleBox_ = nullptr;

    QCheckBox* smoothChk_ = nullptr;
    QDoubleSpinBox* insetBox_ = nullptr;
    QDoubleSpinBox* outlineBox_ = nullptr;

    QPushButton* regenBtn_ = nullptr;

    void emitParams();
    void emitVisuals();
};
