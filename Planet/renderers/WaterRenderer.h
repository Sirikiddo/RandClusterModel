#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QtOpenGL>

#include "renderers/HexSphereRenderer.h"

class WaterRenderer {
public:
    struct Resources {
        GLuint envCubemap = 0;
        GLuint sceneDepthTexture = 0;
        GLuint planetRadiusAtlas = 0;
        GLuint planetSurfaceKindAtlas = 0;
        GLuint planetShoreDistanceAtlas = 0;
        GLuint vao = 0;
        GLsizei indexCount = 0;
    };

    WaterRenderer(QOpenGLFunctions_3_3_Core* gl, GLuint program);

    void render(const HexSphereRenderer::RenderContext& ctx, const Resources& resources) const;

private:
    GLint uniform(const char* name) const;

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint program_ = 0;

    GLint uMvp_ = -1;
    GLint uLightDir_ = -1;
    GLint uViewPos_ = -1;
    GLint uInvViewProjection_ = -1;
    GLint uViewProjection_ = -1;
    GLint uViewportSize_ = -1;
    GLint uWaterSurfaceRadius_ = -1;
    GLint uWaterShellInnerRadius_ = -1;
    GLint uWaterShellOuterRadius_ = -1;
    GLint uBottomEpsilon_ = -1;
    GLint uBoundEpsilon_ = -1;
    GLint uHitEpsilon_ = -1;
    GLint uTerrainContactEpsilon_ = -1;
    GLint uShoreFadeWorldWidth_ = -1;
    GLint uEffectiveWaveAmplitude_ = -1;
    GLint uMaximumPhaseGradient_ = -1;
    GLint uMaximumWaveAngularGradient_ = -1;
    GLint uMaximumAmplitudeGradient_ = -1;
    GLint uMicroFrequency_ = -1;
    GLint uMicroNormalStrength_ = -1;
    GLint uMicroDrag_ = -1;
    GLint uCrestSharpness_ = -1;
    GLint uWaveShapeExponent_ = -1;
    GLint uWaveShapeMean_ = -1;
    GLint uWaveTroughRatio_ = -1;
    GLint uWaveInteractionStrength_ = -1;
    GLint uWaveInteractionThirdStrength_ = -1;
    GLint uWaveCarrierVariance_ = -1;
    GLint uWaveCarrierThirdMoment_ = -1;
    GLint uWaveAxis_ = -1;
    GLint uWaveBreakupAxis_ = -1;
    GLint uWaveFrequency_ = -1;
    GLint uWaveBreakupFrequency_ = -1;
    GLint uWavePhase_ = -1;
    GLint uWaveBreakupPhase_ = -1;
    GLint uWaveMicroPhase_ = -1;
    GLint uWaveBreakupStrength_ = -1;
    GLint uWaveWeight_ = -1;
    GLint uWavePacketCenter_ = -1;
    GLint uWaveActivity_ = -1;
    GLint uWavePacketInnerCosine_ = -1;
    GLint uWavePacketOuterCosine_ = -1;
    GLint uWavePacketContrast_ = -1;
    GLint uWaveDriverSlots_ = -1;
    GLint uWaveDriverComponent_ = -1;
    GLint uWaveDriverFrequency_ = -1;
    GLint uWaveDriverPhase_ = -1;
    GLint uWaveCouplingOffset0_ = -1;
    GLint uWaveCouplingOffset1_ = -1;
    GLint uWaveCoupling_ = -1;
    GLint uDepthOpticalDensity_ = -1;
    GLint uDepthAlphaDensity_ = -1;
    GLint uFresnelStrength_ = -1;
    GLint uSpecularIntensity_ = -1;
    GLint uGlintIntensity_ = -1;
    GLint uGlintThreshold_ = -1;
    GLint uGlintSharpness_ = -1;
    GLint uFoamIntensity_ = -1;
    GLint uRoughness_ = -1;
    GLint uReflectionStrength_ = -1;
    GLint uOpacity_ = -1;
    GLint uShallowColor_ = -1;
    GLint uDeepColor_ = -1;
    GLint uEnvMap_ = -1;
    GLint uSceneDepthTexture_ = -1;
    GLint uPlanetRadiusAtlas_ = -1;
    GLint uPlanetSurfaceKindAtlas_ = -1;
    GLint uPlanetShoreDistanceAtlas_ = -1;
};
