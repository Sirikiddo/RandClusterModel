const float WATER_PI = 3.14159265358979323846;

float smootherstep01(float value) {
    float t = saturate(value);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float smootherstepRange(float edge0, float edge1, float value) {
    return smootherstep01((value - edge0) / max(edge1 - edge0, 1e-6));
}

float smootherstepRangeDerivative(float edge0, float edge1, float value) {
    if (value <= edge0 || value >= edge1) return 0.0;
    float t = (value - edge0) / max(edge1 - edge0, 1e-6);
    return 30.0 * t * t * (t - 1.0) * (t - 1.0) / max(edge1 - edge0, 1e-6);
}

float boundedWaveFromExponential(float exponentialValue) {
    return (exponentialValue - uWaveShapeMean) / (1.0 - uWaveShapeMean);
}

int waveBand(int componentIndex) {
    if (componentIndex < 5) return 0;
    if (componentIndex < 9) return 1;
    return 2;
}

float softMinimumP8(float a, float b) {
    if (a <= 0.0 || b <= 0.0) return 0.0;
    float scale = max(a, b);
    vec2 normalizedValue = vec2(a, b) / scale;
    return scale / pow(pow(normalizedValue.x, -8.0) + pow(normalizedValue.y, -8.0), 0.125);
}

float sampleWaveHeight(
    vec3 dir,
    out float carrierHeight,
    out float fieldVariance,
    out float maximumLocalWeight,
    out float effectiveModeCount,
    out float packetSignal,
    out float phaseDisplacement) {
    float basePhase[12];
    float breakupPhase[12];
    float signal[12];
    float localWeight[12];
    vec3 bandShare = vec3(0.0);
    vec3 bandSignalSum = vec3(0.0);

    for (int i = 0; i < 12; ++i) {
        basePhase[i] = uWaveFrequency[i] * uWaterSurfaceRadius * dot(dir, uWaveAxis[i])
            + uWavePhase[i];
        breakupPhase[i] = uWaveBreakupFrequency[i] * uWaterSurfaceRadius
            * dot(dir, uWaveBreakupAxis[i]) + uWaveBreakupPhase[i];
        float packetDot = dot(dir, uWavePacketCenter[i]);
        float envelope = smootherstepRange(
            uWavePacketOuterCosine[i], uWavePacketInnerCosine[i], packetDot);
        signal[i] = uWaveActivity[i] * envelope;
        int band = waveBand(i);
        bandShare[band] += uWaveWeight[i];
        bandSignalSum[band] += uWaveWeight[i] * signal[i];
    }

    vec3 meanSignal = bandSignalSum / max(bandShare, vec3(1e-6));
    float squaredWeightSum = 0.0;
    float cubedWeightSum = 0.0;
    maximumLocalWeight = 0.0;
    packetSignal = 0.0;
    for (int i = 0; i < 12; ++i) {
        int band = waveBand(i);
        localWeight[i] = uWaveWeight[i] * (1.0 + uWavePacketContrast[i]
            * (signal[i] - meanSignal[band]));
        squaredWeightSum += localWeight[i] * localWeight[i];
        cubedWeightSum += localWeight[i] * localWeight[i] * localWeight[i];
        maximumLocalWeight = max(maximumLocalWeight, localWeight[i]);
        packetSignal += localWeight[i] * signal[i];
    }
    fieldVariance = uWaveCarrierVariance * squaredWeightSum;
    float fieldThirdMoment = uWaveCarrierThirdMoment * cubedWeightSum;
    effectiveModeCount = 1.0 / max(squaredWeightSum, 1e-6);

    float driverPhase[4];
    for (int driverSlot = 0; driverSlot < 4; ++driverSlot) {
        int driverIndex = uWaveDriverComponent[driverSlot];
        float phase = uWaveDriverFrequency[driverSlot] * uWaterSurfaceRadius
            * dot(dir, uWaveAxis[driverIndex]) + uWaveDriverPhase[driverSlot];
        driverPhase[driverSlot] = phase;
    }

    carrierHeight = 0.0;
    phaseDisplacement = 0.0;
    for (int i = 0; i < 12; ++i) {
        float displacement = 0.0;
        ivec2 slots = uWaveDriverSlots[i];
        if (slots.x >= 0 && slots.y >= 0) {
            float argument0 = driverPhase[slots.x] + uWaveCouplingOffset0[i];
            float argument1 = driverPhase[slots.y] + uWaveCouplingOffset1[i];
            float shiftedDriver0 = sin(argument0);
            float shiftedDriver1 = sin(argument1);
            displacement = uWaveCoupling[i].x * shiftedDriver0
                + uWaveCoupling[i].y * shiftedDriver1;
        }
        float phase = basePhase[i] + displacement;
        float exponentialValue = exp(uWaveShapeExponent * (sin(phase) - 1.0));
        float carrier = boundedWaveFromExponential(exponentialValue);
        float breakupExponential = exp(
            uWaveShapeExponent * (sin(breakupPhase[i]) - 1.0));
        float breakupCarrier = boundedWaveFromExponential(breakupExponential);
        float breakupFactor = mix(1.0, breakupCarrier, uWaveBreakupStrength);
        carrierHeight += localWeight[i] * carrier * breakupFactor;
        phaseDisplacement += localWeight[i] * abs(displacement);
    }

    float denominator = 1.0
        + uWaveInteractionStrength * (1.0 - fieldVariance)
        + uWaveInteractionThirdStrength * (1.0 - fieldThirdMoment);
    return (carrierHeight + uWaveInteractionStrength
            * (carrierHeight * carrierHeight - fieldVariance)
        + uWaveInteractionThirdStrength
            * (carrierHeight * carrierHeight * carrierHeight - fieldThirdMoment)) / denominator;
}

void sampleWaveHeightGradient(
    vec3 dir,
    out float height,
    out float carrierHeight,
    out float fieldVariance,
    out float maximumLocalWeight,
    out float effectiveModeCount,
    out float packetSignal,
    out float phaseDisplacement,
    out vec3 angularGradient) {
    float basePhase[12];
    vec3 baseGradient[12];
    float breakupPhase[12];
    vec3 breakupGradient[12];
    float signal[12];
    vec3 signalGradient[12];
    float localWeight[12];
    vec3 weightGradient[12];
    vec3 bandShare = vec3(0.0);
    vec3 bandSignalSum = vec3(0.0);
    vec3 bandSignalGradient[3];
    bandSignalGradient[0] = vec3(0.0);
    bandSignalGradient[1] = vec3(0.0);
    bandSignalGradient[2] = vec3(0.0);

    for (int i = 0; i < 12; ++i) {
        float axisDot = dot(dir, uWaveAxis[i]);
        float frequencyRadius = uWaveFrequency[i] * uWaterSurfaceRadius;
        basePhase[i] = frequencyRadius * axisDot + uWavePhase[i];
        baseGradient[i] = frequencyRadius * (uWaveAxis[i] - dir * axisDot);
        float breakupDot = dot(dir, uWaveBreakupAxis[i]);
        float breakupFrequencyRadius = uWaveBreakupFrequency[i] * uWaterSurfaceRadius;
        breakupPhase[i] = breakupFrequencyRadius * breakupDot + uWaveBreakupPhase[i];
        breakupGradient[i] = breakupFrequencyRadius
            * (uWaveBreakupAxis[i] - dir * breakupDot);
        float packetDot = dot(dir, uWavePacketCenter[i]);
        float envelope = smootherstepRange(
            uWavePacketOuterCosine[i], uWavePacketInnerCosine[i], packetDot);
        float envelopeDerivative = smootherstepRangeDerivative(
            uWavePacketOuterCosine[i], uWavePacketInnerCosine[i], packetDot);
        signal[i] = uWaveActivity[i] * envelope;
        signalGradient[i] = uWaveActivity[i] * envelopeDerivative
            * (uWavePacketCenter[i] - dir * packetDot);
        int band = waveBand(i);
        bandShare[band] += uWaveWeight[i];
        bandSignalSum[band] += uWaveWeight[i] * signal[i];
        bandSignalGradient[band] += uWaveWeight[i] * signalGradient[i];
    }

    vec3 meanSignal = bandSignalSum / max(bandShare, vec3(1e-6));
    vec3 meanSignalGradient[3];
    meanSignalGradient[0] = bandSignalGradient[0] / max(bandShare.x, 1e-6);
    meanSignalGradient[1] = bandSignalGradient[1] / max(bandShare.y, 1e-6);
    meanSignalGradient[2] = bandSignalGradient[2] / max(bandShare.z, 1e-6);
    float squaredWeightSum = 0.0;
    float cubedWeightSum = 0.0;
    maximumLocalWeight = 0.0;
    packetSignal = 0.0;
    vec3 varianceGradient = vec3(0.0);
    vec3 thirdMomentGradient = vec3(0.0);
    for (int i = 0; i < 12; ++i) {
        int band = waveBand(i);
        localWeight[i] = uWaveWeight[i] * (1.0 + uWavePacketContrast[i]
            * (signal[i] - meanSignal[band]));
        weightGradient[i] = uWaveWeight[i] * uWavePacketContrast[i]
            * (signalGradient[i] - meanSignalGradient[band]);
        squaredWeightSum += localWeight[i] * localWeight[i];
        cubedWeightSum += localWeight[i] * localWeight[i] * localWeight[i];
        varianceGradient += 2.0 * uWaveCarrierVariance * localWeight[i] * weightGradient[i];
        thirdMomentGradient += 3.0 * uWaveCarrierThirdMoment
            * localWeight[i] * localWeight[i] * weightGradient[i];
        maximumLocalWeight = max(maximumLocalWeight, localWeight[i]);
        packetSignal += localWeight[i] * signal[i];
    }
    fieldVariance = uWaveCarrierVariance * squaredWeightSum;
    float fieldThirdMoment = uWaveCarrierThirdMoment * cubedWeightSum;
    effectiveModeCount = 1.0 / max(squaredWeightSum, 1e-6);

    float driverPhase[4];
    vec3 driverGradient[4];
    for (int driverSlot = 0; driverSlot < 4; ++driverSlot) {
        int driverIndex = uWaveDriverComponent[driverSlot];
        float axisDot = dot(dir, uWaveAxis[driverIndex]);
        float frequencyRadius = uWaveDriverFrequency[driverSlot] * uWaterSurfaceRadius;
        float phase = frequencyRadius * axisDot + uWaveDriverPhase[driverSlot];
        driverPhase[driverSlot] = phase;
        driverGradient[driverSlot] = frequencyRadius
            * (uWaveAxis[driverIndex] - dir * axisDot);
    }

    carrierHeight = 0.0;
    phaseDisplacement = 0.0;
    vec3 carrierGradient = vec3(0.0);
    for (int i = 0; i < 12; ++i) {
        float displacement = 0.0;
        vec3 phaseGradient = baseGradient[i];
        ivec2 slots = uWaveDriverSlots[i];
        if (slots.x >= 0 && slots.y >= 0) {
            float argument0 = driverPhase[slots.x] + uWaveCouplingOffset0[i];
            float argument1 = driverPhase[slots.y] + uWaveCouplingOffset1[i];
            float shiftedSine0 = sin(argument0);
            float shiftedCosine0 = cos(argument0);
            float shiftedSine1 = sin(argument1);
            float shiftedCosine1 = cos(argument1);
            displacement = uWaveCoupling[i].x * shiftedSine0
                + uWaveCoupling[i].y * shiftedSine1;
            phaseGradient += uWaveCoupling[i].x * shiftedCosine0
                    * driverGradient[slots.x]
                + uWaveCoupling[i].y * shiftedCosine1
                    * driverGradient[slots.y];
        }
        float phase = basePhase[i] + displacement;
        float sineValue = sin(phase);
        float exponentialValue = exp(uWaveShapeExponent * (sineValue - 1.0));
        float carrier = boundedWaveFromExponential(exponentialValue);
        float carrierDerivative = uWaveShapeExponent * exponentialValue * cos(phase)
            / (1.0 - uWaveShapeMean);
        float breakupExponential = exp(
            uWaveShapeExponent * (sin(breakupPhase[i]) - 1.0));
        float breakupCarrier = boundedWaveFromExponential(breakupExponential);
        float breakupDerivative = uWaveShapeExponent * breakupExponential
            * cos(breakupPhase[i]) / (1.0 - uWaveShapeMean);
        float breakupFactor = mix(1.0, breakupCarrier, uWaveBreakupStrength);
        float wavelet = carrier * breakupFactor;
        vec3 waveletGradient = carrierDerivative * breakupFactor * phaseGradient
            + carrier * uWaveBreakupStrength * breakupDerivative * breakupGradient[i];
        carrierHeight += localWeight[i] * wavelet;
        carrierGradient += localWeight[i] * waveletGradient
            + wavelet * weightGradient[i];
        phaseDisplacement += localWeight[i] * abs(displacement);
    }

    float denominator = 1.0
        + uWaveInteractionStrength * (1.0 - fieldVariance)
        + uWaveInteractionThirdStrength * (1.0 - fieldThirdMoment);
    height = (carrierHeight + uWaveInteractionStrength
            * (carrierHeight * carrierHeight - fieldVariance)
        + uWaveInteractionThirdStrength
            * (carrierHeight * carrierHeight * carrierHeight - fieldThirdMoment)) / denominator;
    float carrierPartial = (1.0 + 2.0 * uWaveInteractionStrength * carrierHeight) / denominator;
    carrierPartial += 3.0 * uWaveInteractionThirdStrength
        * carrierHeight * carrierHeight / denominator;
    float variancePartial = uWaveInteractionStrength * (height - 1.0) / denominator;
    float thirdMomentPartial = uWaveInteractionThirdStrength * (height - 1.0) / denominator;
    angularGradient = carrierPartial * carrierGradient
        + variancePartial * varianceGradient
        + thirdMomentPartial * thirdMomentGradient;
}

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

vec3 sampleMicroWaveAngularGradient(vec3 dir, vec3 hitPosition) {
    if (uMicroNormalStrength <= 0.0) return vec3(0.0);

    float viewDistance = length(uViewPos - hitPosition);
    float projectionScale = max(abs(uViewProjection[1][1]), 1e-4);
    float worldPerPixel = 2.0 * viewDistance
        / max(uViewportSize.y * projectionScale, 1.0);
    vec3 gradient = vec3(0.0);

    for (int i = 0; i < 12; ++i) {
        for (int octave = 0; octave < 2; ++octave) {
            vec3 axisA;
            vec3 axisB;
            float frequencyA;
            float frequencyB;
            vec2 temporalPhase;
            float octaveAmplitude;
            if (octave == 0) {
                axisA = safeNormalize(
                    uWaveAxis[i] + 0.73 * uWaveBreakupAxis[i], uWaveAxis[i]);
                axisB = safeNormalize(
                    uWaveAxis[i] - 0.73 * uWaveBreakupAxis[i], uWaveBreakupAxis[i]);
                frequencyA = uWaveFrequency[i] * 2.05;
                frequencyB = uWaveFrequency[i] * (2.05 * 1.071);
                temporalPhase = uWaveMicroPhase[i].xy;
                octaveAmplitude = 0.21;
            } else {
                axisA = safeNormalize(
                    uWaveAxis[i] + 0.61 * uWaveBreakupAxis[i], uWaveAxis[i]);
                axisB = safeNormalize(
                    -0.61 * uWaveAxis[i] + uWaveBreakupAxis[i], uWaveBreakupAxis[i]);
                frequencyA = uWaveFrequency[i] * 3.55;
                frequencyB = uWaveFrequency[i] * (3.55 * 0.937);
                temporalPhase = uWaveMicroPhase[i].zw;
                octaveAmplitude = 0.09;
            }

            float frequencyRadiusA = frequencyA * uWaterSurfaceRadius;
            float frequencyRadiusB = frequencyB * uWaterSurfaceRadius;
            float dotA = dot(dir, axisA);
            float dotB = dot(dir, axisB);
            float phaseA = frequencyRadiusA * dotA + temporalPhase.x;
            float phaseB = frequencyRadiusB * dotB + temporalPhase.y;
            float exponentialA = exp(uWaveShapeExponent * (sin(phaseA) - 1.0));
            float exponentialB = exp(uWaveShapeExponent * (sin(phaseB) - 1.0));
            float carrierA = boundedWaveFromExponential(exponentialA);
            float carrierB = boundedWaveFromExponential(exponentialB);
            float derivativeA = uWaveShapeExponent * exponentialA * cos(phaseA)
                / (1.0 - uWaveShapeMean);
            float derivativeB = uWaveShapeExponent * exponentialB * cos(phaseB)
                / (1.0 - uWaveShapeMean);

            // A product of two transverse waves has compact extrema instead
            // of a one-dimensional ridge. Each derivative is filtered by its
            // own projected pixel footprint before normal composition.
            float filterA = 1.0 - smoothstep(1.2, 3.0, frequencyA * worldPerPixel);
            float filterB = 1.0 - smoothstep(1.2, 3.0, frequencyB * worldPerPixel);
            vec3 tangentA = axisA - dir * dotA;
            vec3 tangentB = axisB - dir * dotB;
            vec3 waveletGradient = filterA * derivativeA * carrierB
                    * frequencyRadiusA * tangentA
                + filterB * carrierA * derivativeB
                    * frequencyRadiusB * tangentB;
            gradient += uWaveWeight[i] * octaveAmplitude * waveletGradient;
        }
    }
    return gradient;
}

vec3 reconstructWaterNormal(vec3 hitDir, WaterSurfaceSample surface, vec3 hitPosition) {
    vec3 microGradient = surface.effectiveAmplitude * uMicroNormalStrength
        * sampleMicroWaveAngularGradient(hitDir, hitPosition);
    vec3 macroNormal = safeNormalize(
        hitDir - (surface.angularGradient + microGradient) / max(surface.radius, 1e-5),
        hitDir);
    return safeNormalize(mix(hitDir, macroNormal, surface.shoreMask), hitDir);
}
