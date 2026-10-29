// Поверхность моря: амплитуда с учётом глубины и берега и итоговая нормаль.
// Волновая форма, её градиент и микро-рябь живут в water_wave_field.glsl и общие
// с реками (resources/terrain/terrain.frag).

uniform float uEffectiveWaveAmplitude;
uniform float uShoreFadeWorldWidth;
uniform float uWaveTroughRatio;
uniform float uBottomEpsilon;

void finishWaterSurface(inout WaterSurfaceSample value) {
    value.waveRaw = value.waveNormalized;
    value.shoreMask = smootherstep01(value.terrain.shoreDistance / max(uShoreFadeWorldWidth, 1e-6));
    float requestedLocal = uEffectiveWaveAmplitude * value.shoreMask;
    float availableDepth = max(uWaterSurfaceRadius - value.terrain.radius - uBottomEpsilon, 0.0);
    float depthCap = availableDepth / uWaveTroughRatio;
    value.effectiveAmplitude = softMinimumP8(requestedLocal, depthCap);
    value.waveHeight = value.effectiveAmplitude * value.waveNormalized;
    value.radius = uWaterSurfaceRadius + value.waveHeight;
}

WaterSurfaceSample sampleWaterSurfaceHeightOnly(vec3 dir) {
    WaterSurfaceSample value;
    value.terrain = sampleTerrainAtlas(dir);
    float carrierHeight = 0.0;
    value.waveNormalized = sampleWaveHeight(
        dir,
        carrierHeight,
        value.fieldVariance,
        value.maximumLocalWeight,
        value.effectiveModeCount,
        value.packetSignal,
        value.phaseDisplacement);
    value.angularGradient = vec3(0.0);
    finishWaterSurface(value);
    value.waveRaw = carrierHeight;
    return value;
}

WaterSurfaceSample sampleWaterSurfaceWithGradient(vec3 dir) {
    WaterSurfaceSample value;
    value.terrain = sampleTerrainAtlas(dir);
    float carrierHeight = 0.0;
    sampleWaveHeightGradient(
        dir,
        value.waveNormalized,
        carrierHeight,
        value.fieldVariance,
        value.maximumLocalWeight,
        value.effectiveModeCount,
        value.packetSignal,
        value.phaseDisplacement,
        value.angularGradient);
    finishWaterSurface(value);
    value.waveRaw = carrierHeight;
    value.angularGradient *= value.effectiveAmplitude;
    return value;
}

vec3 reconstructWaterNormal(vec3 hitDir, WaterSurfaceSample surface, vec3 hitPosition) {
    vec3 microGradient = surface.effectiveAmplitude * uMicroNormalStrength
        * sampleMicroWaveAngularGradient(hitDir, hitPosition);
    vec3 macroNormal = safeNormalize(
        hitDir - (surface.angularGradient + microGradient) / max(surface.radius, 1e-5),
        hitDir);
    return safeNormalize(mix(hitDir, macroNormal, surface.shoreMask), hitDir);
}
