#include "renderers/WaterShaderUniforms.h"

#include <QOpenGLFunctions_3_3_Core>

namespace {

    GLint uniformAt(QOpenGLFunctions_3_3_Core* gl, GLuint program, const char* name) {
        return gl != nullptr && program != 0 ? gl->glGetUniformLocation(program, name) : -1;
    }

} // namespace

void WaterShaderLocations::cache(QOpenGLFunctions_3_3_Core* gl, GLuint program) {
    mvp = uniformAt(gl, program, "uMVP");
    lightDir = uniformAt(gl, program, "uLightDir");
    viewPos = uniformAt(gl, program, "uViewPos");
    invViewProjection = uniformAt(gl, program, "uInvViewProjection");
    viewProjection = uniformAt(gl, program, "uViewProjection");
    viewportSize = uniformAt(gl, program, "uViewportSize");

    waterSurfaceRadius = uniformAt(gl, program, "uWaterSurfaceRadius");
    depthOpticalDensity = uniformAt(gl, program, "uDepthOpticalDensity");
    depthAlphaDensity = uniformAt(gl, program, "uDepthAlphaDensity");

    shoreFadeWorldWidth = uniformAt(gl, program, "uShoreFadeWorldWidth");
    effectiveWaveAmplitude = uniformAt(gl, program, "uEffectiveWaveAmplitude");
    microNormalStrength = uniformAt(gl, program, "uMicroNormalStrength");
    microFrequency = uniformAt(gl, program, "uMicroFrequency");
    microDrag = uniformAt(gl, program, "uMicroDrag");
    crestSharpness = uniformAt(gl, program, "uCrestSharpness");
    waveShapeExponent = uniformAt(gl, program, "uWaveShapeExponent");
    waveShapeMean = uniformAt(gl, program, "uWaveShapeMean");
    waveTroughRatio = uniformAt(gl, program, "uWaveTroughRatio");
    waveInteractionStrength = uniformAt(gl, program, "uWaveInteractionStrength");
    waveInteractionThirdStrength = uniformAt(gl, program, "uWaveInteractionThirdStrength");
    waveCarrierVariance = uniformAt(gl, program, "uWaveCarrierVariance");
    waveCarrierThirdMoment = uniformAt(gl, program, "uWaveCarrierThirdMoment");

    waveAxis = uniformAt(gl, program, "uWaveAxis[0]");
    waveBreakupAxis = uniformAt(gl, program, "uWaveBreakupAxis[0]");
    waveFrequency = uniformAt(gl, program, "uWaveFrequency[0]");
    waveBreakupFrequency = uniformAt(gl, program, "uWaveBreakupFrequency[0]");
    wavePhase = uniformAt(gl, program, "uWavePhase[0]");
    waveBreakupPhase = uniformAt(gl, program, "uWaveBreakupPhase[0]");
    waveMicroPhase = uniformAt(gl, program, "uWaveMicroPhase[0]");
    waveBreakupStrength = uniformAt(gl, program, "uWaveBreakupStrength");
    waveWeight = uniformAt(gl, program, "uWaveWeight[0]");
    wavePacketCenter = uniformAt(gl, program, "uWavePacketCenter[0]");
    waveActivity = uniformAt(gl, program, "uWaveActivity[0]");
    wavePacketInnerCosine = uniformAt(gl, program, "uWavePacketInnerCosine[0]");
    wavePacketOuterCosine = uniformAt(gl, program, "uWavePacketOuterCosine[0]");
    wavePacketContrast = uniformAt(gl, program, "uWavePacketContrast[0]");
    waveDriverSlots = uniformAt(gl, program, "uWaveDriverSlots[0]");
    waveDriverComponent = uniformAt(gl, program, "uWaveDriverComponent[0]");
    waveDriverFrequency = uniformAt(gl, program, "uWaveDriverFrequency[0]");
    waveDriverPhase = uniformAt(gl, program, "uWaveDriverPhase[0]");
    waveCouplingOffset0 = uniformAt(gl, program, "uWaveCouplingOffset0[0]");
    waveCouplingOffset1 = uniformAt(gl, program, "uWaveCouplingOffset1[0]");
    waveCoupling = uniformAt(gl, program, "uWaveCoupling[0]");

    fresnelStrength = uniformAt(gl, program, "uFresnelStrength");
    specularIntensity = uniformAt(gl, program, "uSpecularIntensity");
    glintIntensity = uniformAt(gl, program, "uGlintIntensity");
    glintThreshold = uniformAt(gl, program, "uGlintThreshold");
    glintSharpness = uniformAt(gl, program, "uGlintSharpness");
    foamIntensity = uniformAt(gl, program, "uFoamIntensity");
    roughness = uniformAt(gl, program, "uRoughness");
    reflectionStrength = uniformAt(gl, program, "uReflectionStrength");
    opacity = uniformAt(gl, program, "uOpacity");
    shallowColor = uniformAt(gl, program, "uShallowColor");
    deepColor = uniformAt(gl, program, "uDeepColor");
    envMap = uniformAt(gl, program, "uEnvMap");
}

WaterWaveUniformData WaterWaveUniformData::gather(
    const ResolvedWaterWaveSpec& wave,
    double time) {
    WaterWaveUniformData data;
    const WaterWaveFrameState frame = resolveWaterWaveFrameState(wave, time);
    for (int i = 0; i < kWaterWaveComponentCount; ++i) {
        const WaterWaveComponent& component = wave.requested.components[static_cast<size_t>(i)];
        const size_t index = static_cast<size_t>(i);
        data.axes[index * 3u + 0u] = component.axis.x();
        data.axes[index * 3u + 1u] = component.axis.y();
        data.axes[index * 3u + 2u] = component.axis.z();
        data.breakupAxes[index * 3u + 0u] = component.breakupAxis.x();
        data.breakupAxes[index * 3u + 1u] = component.breakupAxis.y();
        data.breakupAxes[index * 3u + 2u] = component.breakupAxis.z();
        const QVector3D& packetCenter = frame.packetCenters[index];
        data.packetCenters[index * 3u + 0u] = packetCenter.x();
        data.packetCenters[index * 3u + 1u] = packetCenter.y();
        data.packetCenters[index * 3u + 2u] = packetCenter.z();
        data.frequencies[index] = wave.effectiveGeometryFrequency * component.frequencyMultiplier;
        data.breakupFrequencies[index] = wave.effectiveGeometryFrequency
            * component.breakupFrequencyMultiplier;
        data.phases[index] = frame.temporalPhases[index];
        data.breakupPhases[index] = frame.breakupTemporalPhases[index];
        for (size_t microMode = 0; microMode < 4u; ++microMode) {
            data.microPhases[index * 4u + microMode]
                = frame.microTemporalPhases[index][microMode];
        }
        data.weights[index] = component.weight;
        data.activities[index] = frame.activities[index];
        data.innerCosines[index] = component.packetInnerCosine;
        data.outerCosines[index] = component.packetOuterCosine;
        data.contrasts[index] = component.packetContrast;
        data.driverSlots[index * 2u + 0u] = component.driverSlots[0];
        data.driverSlots[index * 2u + 1u] = component.driverSlots[1];
        data.couplingOffsets0[index] = component.couplingPhaseOffsets[0];
        data.couplingOffsets1[index] = component.couplingPhaseOffsets[1];
        data.coupling[index * 2u + 0u] = frame.coupling0[index];
        data.coupling[index * 2u + 1u] = frame.coupling1[index];
    }
    for (int i = 0; i < kWaterWaveDriverCount; ++i) {
        const size_t index = static_cast<size_t>(i);
        const int componentIndex = wave.requested.driverComponentIndices[index];
        data.driverComponents[index] = componentIndex;
        data.driverFrequencies[index] = wave.requested.couplingDriverFrequencyScale
            * wave.effectiveGeometryFrequency
            * wave.requested.components[static_cast<size_t>(componentIndex)].frequencyMultiplier;
        data.driverPhases[index] = frame.driverTemporalPhases[index];
    }
    return data;
}

void WaterWaveUniformData::upload(
    QOpenGLFunctions_3_3_Core* gl,
    const WaterShaderLocations& locations) const {
    if (gl == nullptr) {
        return;
    }
    gl->glUniform3fv(locations.waveAxis, kWaterWaveComponentCount, axes.data());
    gl->glUniform3fv(locations.waveBreakupAxis, kWaterWaveComponentCount, breakupAxes.data());
    gl->glUniform1fv(locations.waveFrequency, kWaterWaveComponentCount, frequencies.data());
    gl->glUniform1fv(locations.waveBreakupFrequency, kWaterWaveComponentCount, breakupFrequencies.data());
    gl->glUniform1fv(locations.wavePhase, kWaterWaveComponentCount, phases.data());
    gl->glUniform1fv(locations.waveBreakupPhase, kWaterWaveComponentCount, breakupPhases.data());
    gl->glUniform4fv(locations.waveMicroPhase, kWaterWaveComponentCount, microPhases.data());
    gl->glUniform1fv(locations.waveWeight, kWaterWaveComponentCount, weights.data());
    gl->glUniform3fv(locations.wavePacketCenter, kWaterWaveComponentCount, packetCenters.data());
    gl->glUniform1fv(locations.waveActivity, kWaterWaveComponentCount, activities.data());
    gl->glUniform1fv(locations.wavePacketInnerCosine, kWaterWaveComponentCount, innerCosines.data());
    gl->glUniform1fv(locations.wavePacketOuterCosine, kWaterWaveComponentCount, outerCosines.data());
    gl->glUniform1fv(locations.wavePacketContrast, kWaterWaveComponentCount, contrasts.data());
    gl->glUniform2iv(locations.waveDriverSlots, kWaterWaveComponentCount, driverSlots.data());
    gl->glUniform1iv(locations.waveDriverComponent, kWaterWaveDriverCount, driverComponents.data());
    gl->glUniform1fv(locations.waveDriverFrequency, kWaterWaveDriverCount, driverFrequencies.data());
    gl->glUniform1fv(locations.waveDriverPhase, kWaterWaveDriverCount, driverPhases.data());
    gl->glUniform1fv(locations.waveCouplingOffset0, kWaterWaveComponentCount, couplingOffsets0.data());
    gl->glUniform1fv(locations.waveCouplingOffset1, kWaterWaveComponentCount, couplingOffsets1.data());
    gl->glUniform2fv(locations.waveCoupling, kWaterWaveComponentCount, coupling.data());
}

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
    const WaterParams& water) {
    if (gl == nullptr) {
        return;
    }
    gl->glUniformMatrix4fv(locations.mvp, 1, GL_FALSE, mvp.constData());
    gl->glUniform3f(locations.lightDir, lightDir.x(), lightDir.y(), lightDir.z());
    gl->glUniform3f(locations.viewPos, viewPos.x(), viewPos.y(), viewPos.z());
    gl->glUniformMatrix4fv(locations.viewProjection, 1, GL_FALSE, viewProjection.constData());
    gl->glUniformMatrix4fv(locations.invViewProjection, 1, GL_FALSE, invViewProjection.constData());
    gl->glUniform2f(locations.viewportSize,
        static_cast<float>(viewportSize.width()),
        static_cast<float>(viewportSize.height()));
    gl->glUniform1f(locations.waterSurfaceRadius, waterSurfaceRadius);

    gl->glUniform1f(locations.depthOpticalDensity, water.depthOpticalDensity);
    gl->glUniform1f(locations.depthAlphaDensity, water.depthAlphaDensity);
    gl->glUniform1f(locations.fresnelStrength, water.fresnelStrength);
    gl->glUniform1f(locations.specularIntensity, water.specularIntensity);
    gl->glUniform1f(locations.glintIntensity, water.glintIntensity);
    gl->glUniform1f(locations.glintThreshold, water.glintThreshold);
    gl->glUniform1f(locations.glintSharpness, water.glintSharpness);
    gl->glUniform1f(locations.foamIntensity, water.foamIntensity);
    gl->glUniform1f(locations.roughness, water.roughness);
    gl->glUniform1f(locations.reflectionStrength, water.reflectionStrength);
    gl->glUniform1f(locations.opacity, water.opacity);
    gl->glUniform3f(locations.shallowColor,
        water.shallowColor.x(), water.shallowColor.y(), water.shallowColor.z());
    gl->glUniform3f(locations.deepColor,
        water.deepColor.x(), water.deepColor.y(), water.deepColor.z());
}

void uploadWaterWaveUniforms(
    QOpenGLFunctions_3_3_Core* gl,
    const WaterShaderLocations& locations,
    const ResolvedWaterWaveSpec& wave,
    const WaterWaveUniformData& data) {
    if (gl == nullptr) {
        return;
    }
    gl->glUniform1f(locations.shoreFadeWorldWidth, wave.requested.shoreFadeWorldWidth);
    gl->glUniform1f(locations.effectiveWaveAmplitude, wave.effectiveGeometryAmplitude);
    gl->glUniform1f(locations.microNormalStrength, wave.microNormalStrength);
    gl->glUniform1f(locations.microFrequency, wave.microFrequency);
    gl->glUniform1f(locations.microDrag, wave.microDrag);
    gl->glUniform1f(locations.crestSharpness, wave.crestSharpness);
    gl->glUniform1f(locations.waveShapeExponent, wave.requested.shape.exponent);
    gl->glUniform1f(locations.waveShapeMean, wave.requested.shape.mean);
    gl->glUniform1f(locations.waveTroughRatio, wave.requested.interaction.troughRatio);
    gl->glUniform1f(locations.waveInteractionStrength, wave.requested.interaction.strength);
    gl->glUniform1f(locations.waveInteractionThirdStrength, wave.requested.interaction.thirdMomentStrength);
    gl->glUniform1f(locations.waveCarrierVariance, wave.requested.interaction.carrierVariance);
    gl->glUniform1f(locations.waveCarrierThirdMoment, wave.requested.interaction.carrierThirdMoment);
    gl->glUniform1f(locations.waveBreakupStrength, wave.requested.crestBreakupStrength);
    data.upload(gl, locations);
}

void bindWaterEnvCubemap(
    QOpenGLFunctions_3_3_Core* gl,
    GLenum textureUnit,
    GLuint cubemap,
    GLint samplerLocation) {
    if (gl == nullptr || cubemap == 0) {
        return;
    }
    gl->glActiveTexture(textureUnit);
    gl->glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
    gl->glUniform1i(samplerLocation, static_cast<GLint>(textureUnit - GL_TEXTURE0));
}
