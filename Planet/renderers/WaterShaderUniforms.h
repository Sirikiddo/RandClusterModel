#pragma once

#include <array>

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QSize>
#include <QVector3D>

#include "generation/TerrainGenerator.h"
#include "generation/WaterWaveModel.h"

class QOpenGLFunctions_3_3_Core;

// Общие для моря и рек uniform-ы.
//
// Море (water_shell.frag) и река (resources/terrain/terrain.frag) подключают
// один и тот же волновой include и одну и ту же функцию затенения, поэтому
// значения обязаны приходить из одного места. Здесь и только здесь решается,
// что именно уходит в шейдеры: light/view, радиус воды, оптические параметры,
// форма волн и массивы компонент.
struct WaterShaderLocations {
    GLint mvp = -1;
    GLint lightDir = -1;
    GLint viewPos = -1;
    GLint invViewProjection = -1;
    GLint viewProjection = -1;
    GLint viewportSize = -1;

    GLint waterSurfaceRadius = -1;
    GLint depthOpticalDensity = -1;
    GLint depthAlphaDensity = -1;

    GLint shoreFadeWorldWidth = -1;
    GLint effectiveWaveAmplitude = -1;
    GLint microNormalStrength = -1;
    GLint microFrequency = -1;
    GLint microDrag = -1;
    GLint crestSharpness = -1;
    GLint waveShapeExponent = -1;
    GLint waveShapeMean = -1;
    GLint waveTroughRatio = -1;
    GLint waveInteractionStrength = -1;
    GLint waveInteractionThirdStrength = -1;
    GLint waveCarrierVariance = -1;
    GLint waveCarrierThirdMoment = -1;

    GLint waveAxis = -1;
    GLint waveBreakupAxis = -1;
    GLint waveFrequency = -1;
    GLint waveBreakupFrequency = -1;
    GLint wavePhase = -1;
    GLint waveBreakupPhase = -1;
    GLint waveMicroPhase = -1;
    GLint waveBreakupStrength = -1;
    GLint waveWeight = -1;
    GLint wavePacketCenter = -1;
    GLint waveActivity = -1;
    GLint wavePacketInnerCosine = -1;
    GLint wavePacketOuterCosine = -1;
    GLint wavePacketContrast = -1;
    GLint waveDriverSlots = -1;
    GLint waveDriverComponent = -1;
    GLint waveDriverFrequency = -1;
    GLint waveDriverPhase = -1;
    GLint waveCouplingOffset0 = -1;
    GLint waveCouplingOffset1 = -1;
    GLint waveCoupling = -1;

    GLint fresnelStrength = -1;
    GLint specularIntensity = -1;
    GLint glintIntensity = -1;
    GLint glintThreshold = -1;
    GLint glintSharpness = -1;
    GLint foamIntensity = -1;
    GLint roughness = -1;
    GLint reflectionStrength = -1;
    GLint opacity = -1;
    GLint shallowColor = -1;
    GLint deepColor = -1;
    GLint envMap = -1;

    // Кэширует локации всех перечисленных uniform-ов для программы.
    // Отсутствующие в шейдере uniform-ы остаются равными -1.
    void cache(QOpenGLFunctions_3_3_Core* gl, GLuint program);
};

// Волновые массивы кадра (12 компонент). Собираются один раз на кадр и
// загружаются в каждую программу, которая рисует воду.
struct WaterWaveUniformData {
    std::array<float, kWaterWaveComponentCount * 3> axes{};
    std::array<float, kWaterWaveComponentCount * 3> breakupAxes{};
    std::array<float, kWaterWaveComponentCount * 3> packetCenters{};
    std::array<float, kWaterWaveComponentCount> frequencies{};
    std::array<float, kWaterWaveComponentCount> breakupFrequencies{};
    std::array<float, kWaterWaveComponentCount> phases{};
    std::array<float, kWaterWaveComponentCount> breakupPhases{};
    std::array<float, kWaterWaveComponentCount * 4> microPhases{};
    std::array<float, kWaterWaveComponentCount> weights{};
    std::array<float, kWaterWaveComponentCount> activities{};
    std::array<float, kWaterWaveComponentCount> innerCosines{};
    std::array<float, kWaterWaveComponentCount> outerCosines{};
    std::array<float, kWaterWaveComponentCount> contrasts{};
    std::array<GLint, kWaterWaveComponentCount * 2> driverSlots{};
    std::array<GLint, kWaterWaveDriverCount> driverComponents{};
    std::array<float, kWaterWaveDriverCount> driverFrequencies{};
    std::array<float, kWaterWaveDriverCount> driverPhases{};
    std::array<float, kWaterWaveComponentCount> couplingOffsets0{};
    std::array<float, kWaterWaveComponentCount> couplingOffsets1{};
    std::array<float, kWaterWaveComponentCount * 2> coupling{};

    static WaterWaveUniformData gather(const ResolvedWaterWaveSpec& wave, double time);

    void upload(QOpenGLFunctions_3_3_Core* gl, const WaterShaderLocations& locations) const;
};

// Свет, камера, радиус поверхности воды и оптические параметры.
void uploadWaterFrameUniforms(
    QOpenGLFunctions_3_3_Core* gl,
    const WaterShaderLocations& locations,
    const QMatrix4x4& mvp,
    const QVector3D& lightDir,
    const QVector3D& viewPos,
    const QMatrix4x4& viewProjection,
    const QMatrix4x4& invViewProjection,
    const QSize& viewportSize,
    float waterSurfaceRadius,
    const WaterParams& water);

// Волновая форма, амплитуды и массивы компонент текущего кадра.
void uploadWaterWaveUniforms(
    QOpenGLFunctions_3_3_Core* gl,
    const WaterShaderLocations& locations,
    const ResolvedWaterWaveSpec& wave,
    const WaterWaveUniformData& data);

// Отражения неба: тот же env cubemap, что и у моря.
void bindWaterEnvCubemap(
    QOpenGLFunctions_3_3_Core* gl,
    GLenum textureUnit,
    GLuint cubemap,
    GLint samplerLocation);
