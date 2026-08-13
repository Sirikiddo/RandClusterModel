#include "renderers/WaterRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "generation/TerrainGenerator.h"
#include "generation/WaterWaveModel.h"

WaterRenderer::WaterRenderer(
    QOpenGLFunctions_3_3_Core* gl,
    GLuint program)
    : gl_(gl)
    , program_(program) {
    if (!gl_ || program_ == 0) return;

    uMvp_ = uniform("uMVP");
    uLightDir_ = uniform("uLightDir");
    uViewPos_ = uniform("uViewPos");
    uInvViewProjection_ = uniform("uInvViewProjection");
    uViewProjection_ = uniform("uViewProjection");
    uViewportSize_ = uniform("uViewportSize");
    uWaterSurfaceRadius_ = uniform("uWaterSurfaceRadius");
    uWaterShellInnerRadius_ = uniform("uWaterShellInnerRadius");
    uWaterShellOuterRadius_ = uniform("uWaterShellOuterRadius");
    uBottomEpsilon_ = uniform("uBottomEpsilon");
    uBoundEpsilon_ = uniform("uBoundEpsilon");
    uHitEpsilon_ = uniform("uHitEpsilon");
    uTerrainContactEpsilon_ = uniform("uTerrainContactEpsilon");
    uShoreFadeWorldWidth_ = uniform("uShoreFadeWorldWidth");
    uEffectiveWaveAmplitude_ = uniform("uEffectiveWaveAmplitude");
    uMaximumPhaseGradient_ = uniform("uMaximumPhaseGradient");
    uMaximumWaveAngularGradient_ = uniform("uMaximumWaveAngularGradient");
    uMaximumAmplitudeGradient_ = uniform("uMaximumAmplitudeGradient");
    uMicroFrequency_ = uniform("uMicroFrequency");
    uMicroNormalStrength_ = uniform("uMicroNormalStrength");
    uMicroDrag_ = uniform("uMicroDrag");
    uCrestSharpness_ = uniform("uCrestSharpness");
    uWaveShapeExponent_ = uniform("uWaveShapeExponent");
    uWaveShapeMean_ = uniform("uWaveShapeMean");
    uWaveTroughRatio_ = uniform("uWaveTroughRatio");
    uWaveInteractionStrength_ = uniform("uWaveInteractionStrength");
    uWaveInteractionThirdStrength_ = uniform("uWaveInteractionThirdStrength");
    uWaveCarrierVariance_ = uniform("uWaveCarrierVariance");
    uWaveCarrierThirdMoment_ = uniform("uWaveCarrierThirdMoment");
    uWaveAxis_ = uniform("uWaveAxis[0]");
    uWaveBreakupAxis_ = uniform("uWaveBreakupAxis[0]");
    uWaveFrequency_ = uniform("uWaveFrequency[0]");
    uWaveBreakupFrequency_ = uniform("uWaveBreakupFrequency[0]");
    uWavePhase_ = uniform("uWavePhase[0]");
    uWaveBreakupPhase_ = uniform("uWaveBreakupPhase[0]");
    uWaveMicroPhase_ = uniform("uWaveMicroPhase[0]");
    uWaveBreakupStrength_ = uniform("uWaveBreakupStrength");
    uWaveWeight_ = uniform("uWaveWeight[0]");
    uWavePacketCenter_ = uniform("uWavePacketCenter[0]");
    uWaveActivity_ = uniform("uWaveActivity[0]");
    uWavePacketInnerCosine_ = uniform("uWavePacketInnerCosine[0]");
    uWavePacketOuterCosine_ = uniform("uWavePacketOuterCosine[0]");
    uWavePacketContrast_ = uniform("uWavePacketContrast[0]");
    uWaveDriverSlots_ = uniform("uWaveDriverSlots[0]");
    uWaveDriverComponent_ = uniform("uWaveDriverComponent[0]");
    uWaveDriverFrequency_ = uniform("uWaveDriverFrequency[0]");
    uWaveDriverPhase_ = uniform("uWaveDriverPhase[0]");
    uWaveCouplingOffset0_ = uniform("uWaveCouplingOffset0[0]");
    uWaveCouplingOffset1_ = uniform("uWaveCouplingOffset1[0]");
    uWaveCoupling_ = uniform("uWaveCoupling[0]");
    uDepthOpticalDensity_ = uniform("uDepthOpticalDensity");
    uDepthAlphaDensity_ = uniform("uDepthAlphaDensity");
    uFresnelStrength_ = uniform("uFresnelStrength");
    uSpecularIntensity_ = uniform("uSpecularIntensity");
    uGlintIntensity_ = uniform("uGlintIntensity");
    uGlintThreshold_ = uniform("uGlintThreshold");
    uGlintSharpness_ = uniform("uGlintSharpness");
    uFoamIntensity_ = uniform("uFoamIntensity");
    uRoughness_ = uniform("uRoughness");
    uReflectionStrength_ = uniform("uReflectionStrength");
    uOpacity_ = uniform("uOpacity");
    uShallowColor_ = uniform("uShallowColor");
    uDeepColor_ = uniform("uDeepColor");
    uEnvMap_ = uniform("uEnvMap");
    uSceneDepthTexture_ = uniform("uSceneDepthTex");
    uPlanetRadiusAtlas_ = uniform("uPlanetRadiusAtlas");
    uPlanetSurfaceKindAtlas_ = uniform("uPlanetSurfaceKindAtlas");
    uPlanetShoreDistanceAtlas_ = uniform("uPlanetShoreDistanceAtlas");
}

GLint WaterRenderer::uniform(const char* name) const {
    return gl_ && program_ != 0 ? gl_->glGetUniformLocation(program_, name) : -1;
}

void WaterRenderer::render(
    const HexSphereRenderer::RenderContext& ctx,
    const Resources& resources) const {
    if (resources.indexCount == 0 || program_ == 0 || !gl_) return;

    const HexSphereModel& model = ctx.graph.scene.model();
    const WaterParams& water = ctx.graph.scene.resolvedWaterParamsValue();
    const ResolvedWaterWaveSpec& wave = ctx.graph.scene.resolvedWaterWaveSpec();

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
    const WaterWaveFrameState frame = resolveWaterWaveFrameState(wave, ctx.lighting.waterTime);
    for (int i = 0; i < kWaterWaveComponentCount; ++i) {
        const WaterWaveComponent& component = wave.requested.components[static_cast<size_t>(i)];
        axes[static_cast<size_t>(i) * 3u + 0u] = component.axis.x();
        axes[static_cast<size_t>(i) * 3u + 1u] = component.axis.y();
        axes[static_cast<size_t>(i) * 3u + 2u] = component.axis.z();
        breakupAxes[static_cast<size_t>(i) * 3u + 0u] = component.breakupAxis.x();
        breakupAxes[static_cast<size_t>(i) * 3u + 1u] = component.breakupAxis.y();
        breakupAxes[static_cast<size_t>(i) * 3u + 2u] = component.breakupAxis.z();
        const QVector3D& packetCenter = frame.packetCenters[static_cast<size_t>(i)];
        packetCenters[static_cast<size_t>(i) * 3u + 0u] = packetCenter.x();
        packetCenters[static_cast<size_t>(i) * 3u + 1u] = packetCenter.y();
        packetCenters[static_cast<size_t>(i) * 3u + 2u] = packetCenter.z();
        frequencies[static_cast<size_t>(i)] = wave.effectiveGeometryFrequency * component.frequencyMultiplier;
        breakupFrequencies[static_cast<size_t>(i)] = wave.effectiveGeometryFrequency
            * component.breakupFrequencyMultiplier;
        phases[static_cast<size_t>(i)] = frame.temporalPhases[static_cast<size_t>(i)];
        breakupPhases[static_cast<size_t>(i)] = frame.breakupTemporalPhases[static_cast<size_t>(i)];
        for (size_t microMode = 0; microMode < 4u; ++microMode) {
            microPhases[static_cast<size_t>(i) * 4u + microMode]
                = frame.microTemporalPhases[static_cast<size_t>(i)][microMode];
        }
        weights[static_cast<size_t>(i)] = component.weight;
        activities[static_cast<size_t>(i)] = frame.activities[static_cast<size_t>(i)];
        innerCosines[static_cast<size_t>(i)] = component.packetInnerCosine;
        outerCosines[static_cast<size_t>(i)] = component.packetOuterCosine;
        contrasts[static_cast<size_t>(i)] = component.packetContrast;
        driverSlots[static_cast<size_t>(i) * 2u + 0u] = component.driverSlots[0];
        driverSlots[static_cast<size_t>(i) * 2u + 1u] = component.driverSlots[1];
        couplingOffsets0[static_cast<size_t>(i)] = component.couplingPhaseOffsets[0];
        couplingOffsets1[static_cast<size_t>(i)] = component.couplingPhaseOffsets[1];
        coupling[static_cast<size_t>(i) * 2u + 0u] = frame.coupling0[static_cast<size_t>(i)];
        coupling[static_cast<size_t>(i) * 2u + 1u] = frame.coupling1[static_cast<size_t>(i)];
    }
    for (int i = 0; i < kWaterWaveDriverCount; ++i) {
        const int componentIndex = wave.requested.driverComponentIndices[static_cast<size_t>(i)];
        driverComponents[static_cast<size_t>(i)] = componentIndex;
        driverFrequencies[static_cast<size_t>(i)] = wave.requested.couplingDriverFrequencyScale
            * wave.effectiveGeometryFrequency
            * wave.requested.components[static_cast<size_t>(componentIndex)].frequencyMultiplier;
        driverPhases[static_cast<size_t>(i)] = frame.driverTemporalPhases[static_cast<size_t>(i)];
    }

    const GLboolean cullWasEnabled = gl_->glIsEnabled(GL_CULL_FACE);
    const GLboolean depthWasEnabled = gl_->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = gl_->glIsEnabled(GL_BLEND);
    GLboolean previousDepthMask = GL_TRUE;
    GLint previousDepthFunc = GL_LESS;
    GLint previousCullFaceMode = GL_BACK;
    GLint previousBlendSrcRgb = GL_ONE;
    GLint previousBlendDstRgb = GL_ZERO;
    GLint previousBlendSrcAlpha = GL_ONE;
    GLint previousBlendDstAlpha = GL_ZERO;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousProgram = 0;
    GLint previousVao = 0;
    gl_->glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    gl_->glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    gl_->glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFaceMode);
    gl_->glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRgb);
    gl_->glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRgb);
    gl_->glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
    gl_->glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);
    gl_->glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    gl_->glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    gl_->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);

    gl_->glUseProgram(program_);
    gl_->glUniformMatrix4fv(uMvp_, 1, GL_FALSE, ctx.mvp.constData());
    gl_->glUniform3f(uLightDir_, ctx.lighting.direction.x(), ctx.lighting.direction.y(), ctx.lighting.direction.z());
    gl_->glUniform3f(uViewPos_, ctx.cameraPos.x(), ctx.cameraPos.y(), ctx.cameraPos.z());
    gl_->glUniformMatrix4fv(uInvViewProjection_, 1, GL_FALSE, ctx.invViewProjection.constData());
    gl_->glUniformMatrix4fv(uViewProjection_, 1, GL_FALSE, ctx.mvp.constData());
    gl_->glUniform2f(uViewportSize_, static_cast<float>(ctx.viewportSize.width()), static_cast<float>(ctx.viewportSize.height()));
    gl_->glUniform1f(uWaterSurfaceRadius_, model.waterSurfaceRadius());
    gl_->glUniform1f(uWaterShellInnerRadius_, wave.bounds.innerRadius);
    gl_->glUniform1f(uWaterShellOuterRadius_, wave.bounds.outerRadius);
    gl_->glUniform1f(uBottomEpsilon_, wave.bounds.bottomEpsilon);
    gl_->glUniform1f(uBoundEpsilon_, wave.bounds.boundEpsilon);
    gl_->glUniform1f(uHitEpsilon_, std::max(wave.bounds.boundEpsilon * 0.25f, 1e-6f));
    gl_->glUniform1f(uTerrainContactEpsilon_, std::max(wave.bounds.bottomEpsilon, 1e-5f));
    gl_->glUniform1f(uShoreFadeWorldWidth_, wave.requested.shoreFadeWorldWidth);
    gl_->glUniform1f(uEffectiveWaveAmplitude_, wave.effectiveGeometryAmplitude);
    gl_->glUniform1f(uMaximumPhaseGradient_, wave.maximumPhaseGradient);
    gl_->glUniform1f(uMaximumWaveAngularGradient_, wave.maximumWaveAngularGradient);
    gl_->glUniform1f(uMaximumAmplitudeGradient_, wave.maximumAmplitudeAngularGradient);
    gl_->glUniform1f(uMicroFrequency_, wave.microFrequency);
    gl_->glUniform1f(uMicroNormalStrength_, wave.microNormalStrength);
    gl_->glUniform1f(uMicroDrag_, wave.microDrag);
    gl_->glUniform1f(uCrestSharpness_, wave.crestSharpness);
    gl_->glUniform1f(uWaveShapeExponent_, wave.requested.shape.exponent);
    gl_->glUniform1f(uWaveShapeMean_, wave.requested.shape.mean);
    gl_->glUniform1f(uWaveTroughRatio_, wave.requested.interaction.troughRatio);
    gl_->glUniform1f(uWaveInteractionStrength_, wave.requested.interaction.strength);
    gl_->glUniform1f(uWaveInteractionThirdStrength_, wave.requested.interaction.thirdMomentStrength);
    gl_->glUniform1f(uWaveCarrierVariance_, wave.requested.interaction.carrierVariance);
    gl_->glUniform1f(uWaveCarrierThirdMoment_, wave.requested.interaction.carrierThirdMoment);
    gl_->glUniform3fv(uWaveAxis_, kWaterWaveComponentCount, axes.data());
    gl_->glUniform3fv(uWaveBreakupAxis_, kWaterWaveComponentCount, breakupAxes.data());
    gl_->glUniform1fv(uWaveFrequency_, kWaterWaveComponentCount, frequencies.data());
    gl_->glUniform1fv(uWaveBreakupFrequency_, kWaterWaveComponentCount, breakupFrequencies.data());
    gl_->glUniform1fv(uWavePhase_, kWaterWaveComponentCount, phases.data());
    gl_->glUniform1fv(uWaveBreakupPhase_, kWaterWaveComponentCount, breakupPhases.data());
    gl_->glUniform4fv(uWaveMicroPhase_, kWaterWaveComponentCount, microPhases.data());
    gl_->glUniform1f(uWaveBreakupStrength_, wave.requested.crestBreakupStrength);
    gl_->glUniform1fv(uWaveWeight_, kWaterWaveComponentCount, weights.data());
    gl_->glUniform3fv(uWavePacketCenter_, kWaterWaveComponentCount, packetCenters.data());
    gl_->glUniform1fv(uWaveActivity_, kWaterWaveComponentCount, activities.data());
    gl_->glUniform1fv(uWavePacketInnerCosine_, kWaterWaveComponentCount, innerCosines.data());
    gl_->glUniform1fv(uWavePacketOuterCosine_, kWaterWaveComponentCount, outerCosines.data());
    gl_->glUniform1fv(uWavePacketContrast_, kWaterWaveComponentCount, contrasts.data());
    gl_->glUniform2iv(uWaveDriverSlots_, kWaterWaveComponentCount, driverSlots.data());
    gl_->glUniform1iv(uWaveDriverComponent_, kWaterWaveDriverCount, driverComponents.data());
    gl_->glUniform1fv(uWaveDriverFrequency_, kWaterWaveDriverCount, driverFrequencies.data());
    gl_->glUniform1fv(uWaveDriverPhase_, kWaterWaveDriverCount, driverPhases.data());
    gl_->glUniform1fv(uWaveCouplingOffset0_, kWaterWaveComponentCount, couplingOffsets0.data());
    gl_->glUniform1fv(uWaveCouplingOffset1_, kWaterWaveComponentCount, couplingOffsets1.data());
    gl_->glUniform2fv(uWaveCoupling_, kWaterWaveComponentCount, coupling.data());

    gl_->glUniform1f(uDepthOpticalDensity_, water.depthOpticalDensity);
    gl_->glUniform1f(uDepthAlphaDensity_, water.depthAlphaDensity);
    gl_->glUniform1f(uFresnelStrength_, water.fresnelStrength);
    gl_->glUniform1f(uSpecularIntensity_, water.specularIntensity);
    gl_->glUniform1f(uGlintIntensity_, water.glintIntensity);
    gl_->glUniform1f(uGlintThreshold_, water.glintThreshold);
    gl_->glUniform1f(uGlintSharpness_, water.glintSharpness);
    gl_->glUniform1f(uFoamIntensity_, water.foamIntensity);
    gl_->glUniform1f(uRoughness_, water.roughness);
    gl_->glUniform1f(uReflectionStrength_, water.reflectionStrength);
    gl_->glUniform1f(uOpacity_, water.opacity);
    gl_->glUniform3f(uShallowColor_, water.shallowColor.x(), water.shallowColor.y(), water.shallowColor.z());
    gl_->glUniform3f(uDeepColor_, water.deepColor.x(), water.deepColor.y(), water.deepColor.z());

    auto bindTexture = [&](GLenum unit, GLenum target, GLuint texture, GLint sampler) {
        gl_->glActiveTexture(unit);
        gl_->glBindTexture(target, texture);
        gl_->glUniform1i(sampler, static_cast<GLint>(unit - GL_TEXTURE0));
    };
    bindTexture(GL_TEXTURE0, GL_TEXTURE_CUBE_MAP, resources.envCubemap, uEnvMap_);
    bindTexture(GL_TEXTURE1, GL_TEXTURE_2D, resources.sceneDepthTexture, uSceneDepthTexture_);
    bindTexture(GL_TEXTURE2, GL_TEXTURE_CUBE_MAP, resources.planetRadiusAtlas, uPlanetRadiusAtlas_);
    bindTexture(GL_TEXTURE3, GL_TEXTURE_CUBE_MAP, resources.planetSurfaceKindAtlas, uPlanetSurfaceKindAtlas_);
    bindTexture(GL_TEXTURE4, GL_TEXTURE_CUBE_MAP, resources.planetShoreDistanceAtlas, uPlanetShoreDistanceAtlas_);

    gl_->glEnable(GL_BLEND);
    gl_->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl_->glEnable(GL_CULL_FACE);
    gl_->glCullFace(GL_BACK);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glDepthFunc(GL_LEQUAL);
    gl_->glDepthMask(GL_TRUE);

    gl_->glBindVertexArray(resources.vao);
    gl_->glDrawElements(GL_TRIANGLES, resources.indexCount, GL_UNSIGNED_INT, nullptr);
    gl_->glBindVertexArray(static_cast<GLuint>(previousVao));

    gl_->glUseProgram(static_cast<GLuint>(previousProgram));
    gl_->glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    gl_->glBlendFuncSeparate(
        static_cast<GLenum>(previousBlendSrcRgb),
        static_cast<GLenum>(previousBlendDstRgb),
        static_cast<GLenum>(previousBlendSrcAlpha),
        static_cast<GLenum>(previousBlendDstAlpha));
    gl_->glCullFace(static_cast<GLenum>(previousCullFaceMode));
    gl_->glDepthMask(previousDepthMask);
    gl_->glDepthFunc(previousDepthFunc);
    if (depthWasEnabled) gl_->glEnable(GL_DEPTH_TEST); else gl_->glDisable(GL_DEPTH_TEST);
    if (cullWasEnabled) gl_->glEnable(GL_CULL_FACE); else gl_->glDisable(GL_CULL_FACE);
    if (blendWasEnabled) gl_->glEnable(GL_BLEND); else gl_->glDisable(GL_BLEND);
}
