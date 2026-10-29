#version 330 core

out vec4 FragColor;

uniform bool uWavesEnabled;
uniform vec3 uLightDir;
uniform samplerCube uEnvMap;
uniform sampler2D uSceneDepthTex;
uniform samplerCube uPlanetRadiusAtlas;
uniform samplerCube uPlanetSurfaceKindAtlas;
uniform samplerCube uPlanetShoreDistanceAtlas;
uniform mat4 uInvViewProjection;
uniform float uWaterShellInnerRadius;
uniform float uWaterShellOuterRadius;
uniform float uBoundEpsilon;
uniform float uHitEpsilon;
uniform float uTerrainContactEpsilon;
uniform float uDepthOpticalDensity;
uniform float uDepthAlphaDensity;
uniform float uMaximumPhaseGradient;
uniform float uMaximumWaveAngularGradient;
uniform float uMaximumAmplitudeGradient;
uniform float uMicroFrequency;
uniform float uMicroDrag;
uniform float uCrestSharpness;
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

// Порядок важен: water_common.glsl пользуется saturate/safeNormalize из
// water_math.glsl и view-данными из water_wave_field.glsl.
#include "water_math.glsl"
#include "water_wave_field.glsl"
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
    if (!uWavesEnabled) {
        FragColor = shadeFlatWaterOutput(hit, opticalDepth01, alpha);
        return;
    }
    float foamMask = computeFoamMask(hit, opticalDepth01);

    FragColor = shadeWaterOutput(hit, opticalDepth01, foamMask, alpha);
}
