#pragma once

#include <QWidget>

#include "generation/TerrainGenerator.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QPushButton;
class QScrollArea;
class QSpinBox;

class PlanetSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlanetSettingsPanel(QWidget* parent = nullptr);
    void setContributorMode(bool enabled);
    TerrainParams currentTerrainParams() const;
    WaterParams currentWaterParams() const;
    bool currentSmoothOneStep() const;
    double currentStripInset() const;
    double currentOutlineBias() const;

signals:
    void generatorChanged(int index);
    void paramsChanged(const TerrainParams& p);
    void waterParamsChanged(const WaterParams& p);
    void visualizeChanged(bool smoothOneStep, double stripInset, double outlineBias);
    void requestRegenerate();

private:
    QComboBox* genBox_ = nullptr;
    QSpinBox* seedBox_ = nullptr;
    QSpinBox* seaBox_ = nullptr;
    QDoubleSpinBox* scaleBox_ = nullptr;

    QComboBox* waterPresetBox_ = nullptr;
    QDoubleSpinBox* waveStrengthBox_ = nullptr;
    QDoubleSpinBox* fresnelBox_ = nullptr;
    QDoubleSpinBox* specularBox_ = nullptr;
    QDoubleSpinBox* glintIntensityBox_ = nullptr;
    QDoubleSpinBox* glintThresholdBox_ = nullptr;
    QDoubleSpinBox* glintSharpnessBox_ = nullptr;
    QDoubleSpinBox* foamIntensityBox_ = nullptr;
    QDoubleSpinBox* roughnessBox_ = nullptr;
    QDoubleSpinBox* reflectionBox_ = nullptr;
    QDoubleSpinBox* opacityBox_ = nullptr;
    QSpinBox* beachWidthBox_ = nullptr;
    QDoubleSpinBox* octaveDetailBox_ = nullptr;
    QDoubleSpinBox* depthOpticalDensityBox_ = nullptr;
    QDoubleSpinBox* depthAlphaDensityBox_ = nullptr;
    QDoubleSpinBox* relativeHeightBox_ = nullptr;
    QDoubleSpinBox* primaryWaveDensityBox_ = nullptr;
    QDoubleSpinBox* waveSpeedBox_ = nullptr;

    QCheckBox* smoothChk_ = nullptr;
    QDoubleSpinBox* insetBox_ = nullptr;
    QDoubleSpinBox* outlineBox_ = nullptr;

    QPushButton* regenBtn_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QGroupBox* waterSettingsGroup_ = nullptr;
    QWidget* waterSettingsContent_ = nullptr;
    QGroupBox* oceanGroup_ = nullptr;
    QGroupBox* waveGeometryGroup_ = nullptr;

    void emitParams();
    void emitWaterParams();
    void emitVisuals();
    void applyWaterPreset(WaterPreset preset);
};
