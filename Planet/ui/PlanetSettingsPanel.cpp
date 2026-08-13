#include "PlanetSettingsPanel.h"

#include <climits>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

PlanetSettingsPanel::PlanetSettingsPanel(QWidget* parent)
    : QWidget(parent) {
    const TerrainParams defaultTerrain = defaultTerrainParams();
    genBox_ = new QComboBox(this);
    genBox_->addItem("NoOp");
    genBox_->addItem("Sine");
    genBox_->addItem("Perlin");
    genBox_->addItem("Climate");
    genBox_->setCurrentIndex(kDefaultTerrainGeneratorIndex);

    seedBox_ = new QSpinBox(this);
    seedBox_->setRange(0, INT_MAX);
    seedBox_->setValue(static_cast<int>(defaultTerrain.seed));

    seaBox_ = new QSpinBox(this);
    seaBox_->setRange(-100000, 100000);
    seaBox_->setValue(defaultTerrain.seaLevel);

    scaleBox_ = new QDoubleSpinBox(this);
    scaleBox_->setRange(0.0001, 100000.0);
    scaleBox_->setSingleStep(0.1);
    scaleBox_->setValue(defaultTerrain.scale);

    auto* genForm = new QFormLayout;
    genForm->addRow("Generator", genBox_);
    genForm->addRow("Seed", seedBox_);
    genForm->addRow("Sea level", seaBox_);
    genForm->addRow("Scale", scaleBox_);

    auto* genGroup = new QGroupBox("Generation");
    genGroup->setLayout(genForm);

    const WaterParams defaultWaterParams = waterParamsForPreset(WaterPreset::Temperate);

    waterPresetBox_ = new QComboBox(this);
    waterPresetBox_->addItem("Temperate", static_cast<int>(WaterPreset::Temperate));
    waterPresetBox_->addItem("Lagoon", static_cast<int>(WaterPreset::Lagoon));
    waterPresetBox_->addItem("Storm", static_cast<int>(WaterPreset::Storm));
    waterPresetBox_->setCurrentIndex(static_cast<int>(defaultWaterParams.preset));

    waveStrengthBox_ = new QDoubleSpinBox(this);
    waveStrengthBox_->setRange(0.0, 1.0);
    waveStrengthBox_->setSingleStep(0.05);
    waveStrengthBox_->setValue(defaultWaterParams.waveStrength);

    fresnelBox_ = new QDoubleSpinBox(this);
    fresnelBox_->setRange(0.0, 2.5);
    fresnelBox_->setSingleStep(0.05);
    fresnelBox_->setValue(defaultWaterParams.fresnelStrength);

    specularBox_ = new QDoubleSpinBox(this);
    specularBox_->setRange(0.0, 3.0);
    specularBox_->setSingleStep(0.05);
    specularBox_->setValue(defaultWaterParams.specularIntensity);

    glintIntensityBox_ = new QDoubleSpinBox(this);
    glintIntensityBox_->setRange(0.0, 4.0);
    glintIntensityBox_->setSingleStep(0.05);
    glintIntensityBox_->setValue(defaultWaterParams.glintIntensity);

    glintThresholdBox_ = new QDoubleSpinBox(this);
    glintThresholdBox_->setRange(0.0, 0.995);
    glintThresholdBox_->setSingleStep(0.02);
    glintThresholdBox_->setValue(defaultWaterParams.glintThreshold);

    glintSharpnessBox_ = new QDoubleSpinBox(this);
    glintSharpnessBox_->setRange(0.0, 1.0);
    glintSharpnessBox_->setSingleStep(0.05);
    glintSharpnessBox_->setValue(defaultWaterParams.glintSharpness);

    foamIntensityBox_ = new QDoubleSpinBox(this);
    foamIntensityBox_->setRange(0.0, 3.0);
    foamIntensityBox_->setSingleStep(0.05);
    foamIntensityBox_->setValue(defaultWaterParams.foamIntensity);

    roughnessBox_ = new QDoubleSpinBox(this);
    roughnessBox_->setRange(0.02, 1.0);
    roughnessBox_->setSingleStep(0.02);
    roughnessBox_->setValue(defaultWaterParams.roughness);

    reflectionBox_ = new QDoubleSpinBox(this);
    reflectionBox_->setRange(0.0, 1.0);
    reflectionBox_->setSingleStep(0.05);
    reflectionBox_->setValue(defaultWaterParams.reflectionStrength);

    opacityBox_ = new QDoubleSpinBox(this);
    opacityBox_->setRange(0.70, 1.0);
    opacityBox_->setSingleStep(0.05);
    opacityBox_->setValue(defaultWaterParams.opacity);

    beachWidthBox_ = new QSpinBox(this);
    beachWidthBox_->setRange(0, 10000);
    beachWidthBox_->setValue(defaultWaterParams.beachWidth);

    octaveDetailBox_ = new QDoubleSpinBox(this);
    octaveDetailBox_->setRange(0.0, 1.0);
    octaveDetailBox_->setSingleStep(0.02);
    octaveDetailBox_->setValue(defaultWaterParams.octaveDetail);

    depthOpticalDensityBox_ = new QDoubleSpinBox(this);
    depthOpticalDensityBox_->setRange(0.0, 10000.0);
    depthOpticalDensityBox_->setSingleStep(0.1);
    depthOpticalDensityBox_->setValue(defaultWaterParams.depthOpticalDensity);

    depthAlphaDensityBox_ = new QDoubleSpinBox(this);
    depthAlphaDensityBox_->setRange(0.0, 10000.0);
    depthAlphaDensityBox_->setSingleStep(0.1);
    depthAlphaDensityBox_->setValue(defaultWaterParams.depthAlphaDensity);

    relativeHeightBox_ = new QDoubleSpinBox(this);
    relativeHeightBox_->setRange(0.05, 1.00);
    relativeHeightBox_->setSingleStep(0.05);
    relativeHeightBox_->setDecimals(2);
    relativeHeightBox_->setValue(defaultWaterParams.shellWaveAmplitude);

    primaryWaveDensityBox_ = new QDoubleSpinBox(this);
    primaryWaveDensityBox_->setRange(1.0, 36.0);
    primaryWaveDensityBox_->setSingleStep(1.0);
    primaryWaveDensityBox_->setDecimals(1);
    primaryWaveDensityBox_->setValue(defaultWaterParams.shellWaveFrequency);

    waveSpeedBox_ = new QDoubleSpinBox(this);
    waveSpeedBox_->setRange(0.0, 1000.0);
    waveSpeedBox_->setSingleStep(0.02);
    waveSpeedBox_->setValue(defaultWaterParams.shellWaveSpeed);

    auto* oceanForm = new QFormLayout;
    oceanForm->addRow("Preset", waterPresetBox_);
    oceanForm->addRow("Crest sharpness", waveStrengthBox_);
    oceanForm->addRow("Fresnel", fresnelBox_);
    oceanForm->addRow("Specular", specularBox_);
    oceanForm->addRow("Glint", glintIntensityBox_);
    oceanForm->addRow("Glint threshold", glintThresholdBox_);
    oceanForm->addRow("Glint sharpness", glintSharpnessBox_);
    oceanForm->addRow("Foam", foamIntensityBox_);
    oceanForm->addRow("Roughness", roughnessBox_);
    oceanForm->addRow("Reflection", reflectionBox_);
    oceanForm->addRow("Opacity", opacityBox_);
    oceanForm->addRow("Beach width", beachWidthBox_);

    oceanGroup_ = new QGroupBox("Ocean");
    oceanGroup_->setLayout(oceanForm);

    auto* depthForm = new QFormLayout;
    depthForm->addRow("Depth optical", depthOpticalDensityBox_);
    depthForm->addRow("Depth alpha", depthAlphaDensityBox_);
    depthForm->addRow("Relative height/area", relativeHeightBox_);
    depthForm->addRow("Primary waves/hex", primaryWaveDensityBox_);
    depthForm->addRow("Octave detail", octaveDetailBox_);
    depthForm->addRow("Wave speed", waveSpeedBox_);
    waveGeometryGroup_ = new QGroupBox("Wave geometry");
    waveGeometryGroup_->setLayout(depthForm);

    waterSettingsContent_ = new QWidget(this);
    auto* waterContentLayout = new QVBoxLayout;
    waterContentLayout->setContentsMargins(0, 0, 0, 0);
    waterContentLayout->addWidget(oceanGroup_);
    waterContentLayout->addWidget(waveGeometryGroup_);
    waterSettingsContent_->setLayout(waterContentLayout);

    waterSettingsGroup_ = new QGroupBox("Water settings");
    waterSettingsGroup_->setCheckable(true);
    waterSettingsGroup_->setChecked(false);
    auto* waterSettingsLayout = new QVBoxLayout;
    waterSettingsLayout->addWidget(waterSettingsContent_);
    waterSettingsGroup_->setLayout(waterSettingsLayout);
    waterSettingsContent_->setVisible(false);
    connect(waterSettingsGroup_, &QGroupBox::toggled,
            waterSettingsContent_, &QWidget::setVisible);

    smoothChk_ = new QCheckBox("Smooth one-step", this);
    smoothChk_->setChecked(true);

    insetBox_ = new QDoubleSpinBox(this);
    insetBox_->setRange(0.0, 0.49);
    insetBox_->setSingleStep(0.01);
    insetBox_->setValue(0.25);

    outlineBox_ = new QDoubleSpinBox(this);
    outlineBox_->setRange(0.0, 0.1);
    outlineBox_->setSingleStep(0.001);
    outlineBox_->setValue(0.004);

    auto* visForm = new QFormLayout;
    visForm->addRow(smoothChk_);
    visForm->addRow("Strip inset", insetBox_);
    visForm->addRow("Outline bias", outlineBox_);

    auto* visGroup = new QGroupBox("Visual");
    visGroup->setLayout(visForm);

    regenBtn_ = new QPushButton("Regenerate", this);

    auto* content = new QWidget(this);
    auto* contentLay = new QVBoxLayout;
    contentLay->addWidget(genGroup);
    contentLay->addWidget(waterSettingsGroup_);
    contentLay->addWidget(visGroup);
    contentLay->addWidget(regenBtn_);
    contentLay->addStretch(1);
    content->setLayout(contentLay);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea_->setWidget(content);

    auto* lay = new QVBoxLayout;
    lay->addWidget(scrollArea_);
    setLayout(lay);

    connect(genBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        emit generatorChanged(idx);
        emit requestRegenerate();
    });

    auto emitBoth = [this] {
        emitParams();
        emit requestRegenerate();
    };
    connect(seedBox_, qOverload<int>(&QSpinBox::valueChanged), this, [emitBoth](int) { emitBoth(); });
    connect(seaBox_, qOverload<int>(&QSpinBox::valueChanged), this, [emitBoth](int) { emitBoth(); });
    connect(scaleBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitBoth](double) { emitBoth(); });

    auto emitWater = [this] {
        emitWaterParams();
    };
    connect(waterPresetBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        applyWaterPreset(static_cast<WaterPreset>(waterPresetBox_->currentData().toInt()));
    });
    connect(waveStrengthBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(fresnelBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(specularBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(glintIntensityBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(glintThresholdBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(glintSharpnessBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(foamIntensityBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(roughnessBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(reflectionBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(opacityBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(beachWidthBox_, qOverload<int>(&QSpinBox::valueChanged), this, [emitWater](int) { emitWater(); });
    connect(octaveDetailBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(depthOpticalDensityBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(depthAlphaDensityBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(relativeHeightBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(primaryWaveDensityBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });
    connect(waveSpeedBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitWater](double) { emitWater(); });

    auto emitVis = [this] {
        emitVisuals();
    };
    connect(smoothChk_, &QCheckBox::toggled, this, [emitVis](bool) { emitVis(); });
    connect(insetBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitVis](double) { emitVis(); });
    connect(outlineBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitVis](double) { emitVis(); });

    connect(regenBtn_, &QPushButton::clicked, this, [this] {
        emitParams();
        emitWaterParams();
        emitVisuals();
        emit requestRegenerate();
    });

}

void PlanetSettingsPanel::setContributorMode(bool enabled) {
    genBox_->setEnabled(!enabled);
    seedBox_->setEnabled(!enabled);
    seaBox_->setEnabled(!enabled);
    scaleBox_->setEnabled(!enabled);
    waterPresetBox_->setEnabled(!enabled);
    waveStrengthBox_->setEnabled(!enabled);
    fresnelBox_->setEnabled(!enabled);
    specularBox_->setEnabled(!enabled);
    glintIntensityBox_->setEnabled(!enabled);
    glintThresholdBox_->setEnabled(!enabled);
    glintSharpnessBox_->setEnabled(!enabled);
    foamIntensityBox_->setEnabled(!enabled);
    roughnessBox_->setEnabled(!enabled);
    reflectionBox_->setEnabled(!enabled);
    opacityBox_->setEnabled(!enabled);
    beachWidthBox_->setEnabled(!enabled);
    octaveDetailBox_->setEnabled(!enabled);
    depthOpticalDensityBox_->setEnabled(!enabled);
    depthAlphaDensityBox_->setEnabled(!enabled);
    relativeHeightBox_->setEnabled(!enabled);
    primaryWaveDensityBox_->setEnabled(!enabled);
    waveSpeedBox_->setEnabled(!enabled);
    smoothChk_->setEnabled(!enabled);
    insetBox_->setEnabled(!enabled);
    outlineBox_->setEnabled(!enabled);
    regenBtn_->setEnabled(!enabled);
}

TerrainParams PlanetSettingsPanel::currentTerrainParams() const {
    TerrainParams p;
    p.seed = static_cast<uint32_t>(seedBox_->value());
    p.seaLevel = seaBox_->value();
    p.scale = static_cast<float>(scaleBox_->value());
    return p;
}

WaterParams PlanetSettingsPanel::currentWaterParams() const {
    const WaterPreset selectedPreset = static_cast<WaterPreset>(waterPresetBox_->currentData().toInt());
    WaterParams params = waterParamsForPreset(selectedPreset);
    params.waveStrength = static_cast<float>(waveStrengthBox_->value());
    params.fresnelStrength = static_cast<float>(fresnelBox_->value());
    params.specularIntensity = static_cast<float>(specularBox_->value());
    params.glintIntensity = static_cast<float>(glintIntensityBox_->value());
    params.glintThreshold = static_cast<float>(glintThresholdBox_->value());
    params.glintSharpness = static_cast<float>(glintSharpnessBox_->value());
    params.foamIntensity = static_cast<float>(foamIntensityBox_->value());
    params.roughness = static_cast<float>(roughnessBox_->value());
    params.reflectionStrength = static_cast<float>(reflectionBox_->value());
    params.opacity = static_cast<float>(opacityBox_->value());
    params.beachWidth = beachWidthBox_->value();
    params.octaveDetail = static_cast<float>(octaveDetailBox_->value());
    params.depthOpticalDensity = static_cast<float>(depthOpticalDensityBox_->value());
    params.depthAlphaDensity = static_cast<float>(depthAlphaDensityBox_->value());
    params.shellWaveAmplitude = static_cast<float>(relativeHeightBox_->value());
    params.shellWaveFrequency = static_cast<float>(primaryWaveDensityBox_->value());
    params.shellWaveSpeed = static_cast<float>(waveSpeedBox_->value());
    return params;
}

bool PlanetSettingsPanel::currentSmoothOneStep() const {
    return smoothChk_->isChecked();
}

double PlanetSettingsPanel::currentStripInset() const {
    return insetBox_->value();
}

double PlanetSettingsPanel::currentOutlineBias() const {
    return outlineBox_->value();
}

void PlanetSettingsPanel::emitParams() {
    emit paramsChanged(currentTerrainParams());
}

void PlanetSettingsPanel::emitVisuals() {
    emit visualizeChanged(currentSmoothOneStep(), currentStripInset(), currentOutlineBias());
}

void PlanetSettingsPanel::emitWaterParams() {
    emit waterParamsChanged(currentWaterParams());
}

void PlanetSettingsPanel::applyWaterPreset(WaterPreset preset) {
    const WaterParams params = waterParamsForPreset(preset);
    const QList<QObject*> controls = {
        waveStrengthBox_, fresnelBox_, specularBox_, glintIntensityBox_,
        glintThresholdBox_, glintSharpnessBox_, foamIntensityBox_,
        roughnessBox_, reflectionBox_, opacityBox_,
        beachWidthBox_, octaveDetailBox_,
        depthOpticalDensityBox_, depthAlphaDensityBox_, relativeHeightBox_,
        primaryWaveDensityBox_, waveSpeedBox_ };
    for (QObject* control : controls) control->blockSignals(true);

    waveStrengthBox_->setValue(params.waveStrength);
    fresnelBox_->setValue(params.fresnelStrength);
    specularBox_->setValue(params.specularIntensity);
    glintIntensityBox_->setValue(params.glintIntensity);
    glintThresholdBox_->setValue(params.glintThreshold);
    glintSharpnessBox_->setValue(params.glintSharpness);
    foamIntensityBox_->setValue(params.foamIntensity);
    roughnessBox_->setValue(params.roughness);
    reflectionBox_->setValue(params.reflectionStrength);
    opacityBox_->setValue(params.opacity);
    beachWidthBox_->setValue(params.beachWidth);
    octaveDetailBox_->setValue(params.octaveDetail);
    depthOpticalDensityBox_->setValue(params.depthOpticalDensity);
    depthAlphaDensityBox_->setValue(params.depthAlphaDensity);
    relativeHeightBox_->setValue(params.shellWaveAmplitude);
    primaryWaveDensityBox_->setValue(params.shellWaveFrequency);
    waveSpeedBox_->setValue(params.shellWaveSpeed);

    for (QObject* control : controls) control->blockSignals(false);
    emitWaterParams();
}
