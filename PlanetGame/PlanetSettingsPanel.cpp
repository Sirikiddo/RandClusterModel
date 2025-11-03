#include "PlanetSettingsPanel.h"
#include "TerrainGenerator.h"

#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QGroupBox>

PlanetSettingsPanel::PlanetSettingsPanel(QWidget* parent) : QWidget(parent) {
    genBox_ = new QComboBox(this);
    genBox_->addItem("NoOp");
    genBox_->addItem("Sine");
    genBox_->addItem("Perlin");

    seedBox_ = new QSpinBox(this);
    seedBox_->setRange(0, INT_MAX);
    seedBox_->setValue(0);

    seaBox_ = new QSpinBox(this);
    seaBox_->setRange(-50, 50);
    seaBox_->setValue(0);

    scaleBox_ = new QDoubleSpinBox(this);
    scaleBox_->setRange(0.1, 64.0);
    scaleBox_->setSingleStep(0.1);
    scaleBox_->setValue(3.0);

    auto genForm = new QFormLayout;
    genForm->addRow("Generator", genBox_);
    genForm->addRow("Seed", seedBox_);
    genForm->addRow("Sea level", seaBox_);
    genForm->addRow("Scale", scaleBox_);

    auto genGroup = new QGroupBox("Generation");
    genGroup->setLayout(genForm);

    // --- Visual ---
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

    auto visForm = new QFormLayout;
    visForm->addRow(smoothChk_);
    visForm->addRow("Strip inset", insetBox_);
    visForm->addRow("Outline bias", outlineBox_);

    auto visGroup = new QGroupBox("Visual");
    visGroup->setLayout(visForm);

    regenBtn_ = new QPushButton("Regenerate", this);

    auto lay = new QVBoxLayout;
    lay->addWidget(genGroup);
    lay->addWidget(visGroup);
    lay->addWidget(regenBtn_);
    lay->addStretch(1);
    setLayout(lay);

    // signals
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

    auto emitVis = [this] {
        emitVisuals();
        emit requestRegenerate();
        };
    connect(smoothChk_, &QCheckBox::toggled, this, [emitVis](bool) { emitVis(); });
    connect(insetBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitVis](double) { emitVis(); });
    connect(outlineBox_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [emitVis](double) { emitVis(); });

    connect(regenBtn_, &QPushButton::clicked, this, [this] {
        emitParams(); emitVisuals(); emit requestRegenerate();
        });
}

void PlanetSettingsPanel::emitParams() {
    TerrainParams p;
    p.seed = uint32_t(seedBox_->value());
    p.seaLevel = seaBox_->value();
    p.scale = float(scaleBox_->value());
    emit paramsChanged(p);
}

void PlanetSettingsPanel::emitVisuals() {
    emit visualizeChanged(smoothChk_->isChecked(),
        insetBox_->value(),
        outlineBox_->value());
}