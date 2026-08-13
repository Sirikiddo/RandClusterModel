#version 330 core

out vec4 FragColor;

uniform vec3 uLightDir;
uniform vec3 uViewPos;
uniform samplerCube uEnvMap;
uniform sampler2D uSceneDepthTex;
uniform samplerCube uPlanetRadiusAtlas;
uniform samplerCube uPlanetSurfaceKindAtlas;
uniform samplerCube uPlanetShoreDistanceAtlas;
uniform mat4 uInvViewProjection;
uniform mat4 uViewProjection;
uniform vec2 uViewportSize;
uniform float uWaterSurfaceRadius;
uniform float uWaterShellInnerRadius;
uniform float uWaterShellOuterRadius;
uniform float uBottomEpsilon;
uniform float uBoundEpsilon;
uniform float uHitEpsilon;
uniform float uTerrainContactEpsilon;
uniform float uShoreFadeWorldWidth;
uniform float uDepthOpticalDensity;
uniform float uDepthAlphaDensity;
uniform float uEffectiveWaveAmplitude;
uniform float uMaximumPhaseGradient;
uniform float uMaximumWaveAngularGradient;
uniform float uMaximumAmplitudeGradient;
uniform float uMicroFrequency;
uniform float uMicroNormalStrength;
uniform float uMicroDrag;
uniform float uCrestSharpness;
uniform float uWaveShapeExponent;
uniform float uWaveShapeMean;
uniform float uWaveTroughRatio;
uniform float uWaveInteractionStrength;
uniform float uWaveInteractionThirdStrength;
uniform float uWaveCarrierVariance;
uniform float uWaveCarrierThirdMoment;
uniform vec3 uWaveAxis[12];
uniform vec3 uWaveBreakupAxis[12];
uniform float uWaveFrequency[12];
uniform float uWaveBreakupFrequency[12];
uniform float uWavePhase[12];
uniform float uWaveBreakupPhase[12];
uniform vec4 uWaveMicroPhase[12];
uniform float uWaveBreakupStrength;
uniform float uWaveWeight[12];
uniform vec3 uWavePacketCenter[12];
uniform float uWaveActivity[12];
uniform float uWavePacketInnerCosine[12];
uniform float uWavePacketOuterCosine[12];
uniform float uWavePacketContrast[12];
uniform ivec2 uWaveDriverSlots[12];
uniform int uWaveDriverComponent[4];
uniform float uWaveDriverFrequency[4];
uniform float uWaveDriverPhase[4];
uniform float uWaveCouplingOffset0[12];
uniform float uWaveCouplingOffset1[12];
uniform vec2 uWaveCoupling[12];
uniform float uFresnelStrength;
uniform float uSpecularIntensity;
uniform float uGlintIntensity;
uniform float uGlintThreshold;
uniform float uGlintSharpness;
uniform float uFoamIntensity;
uniform float uRoughness;
uniform float uReflectionStrength;
uniform float uOpacity;
uniform vec3 uShallowColor;
uniform vec3 uDeepColor;

#include "water_common.glsl"
#include "water_surface.glsl"
#include "water_intersection.glsl"
#include "water_shading.glsl"

void main() {
    vec2 screenUv = currentScreenUv();
    WaterHit hit = traceWater(screenUv);
    if (!hit.hit) {
        discard;
    }

    vec4 clipPosition = uViewProjection * vec4(hit.position, 1.0);
    float ndcDepth = clipPosition.z / max(clipPosition.w, 1e-6);
    gl_FragDepth = ndcDepth * 0.5 + 0.5;

    float opticalDepth01 = 1.0 - exp(-hit.visibleThickness * uDepthOpticalDensity);
    float alpha = 1.0 - exp(-hit.visibleThickness * uDepthAlphaDensity);
    float foamMask = computeFoamMask(hit, opticalDepth01);

    FragColor = shadeWaterOutput(hit, opticalDepth01, foamMask, alpha);
}
