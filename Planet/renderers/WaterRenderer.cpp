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

    commonLocations_.cache(gl_, program_);

    uWavesEnabled_ = uniform("uWavesEnabled");
    uWaterShellInnerRadius_ = uniform("uWaterShellInnerRadius");
    uWaterShellOuterRadius_ = uniform("uWaterShellOuterRadius");
    uBottomEpsilon_ = uniform("uBottomEpsilon");
    uBoundEpsilon_ = uniform("uBoundEpsilon");
    uHitEpsilon_ = uniform("uHitEpsilon");
    uTerrainContactEpsilon_ = uniform("uTerrainContactEpsilon");
    uMaximumPhaseGradient_ = uniform("uMaximumPhaseGradient");
    uMaximumWaveAngularGradient_ = uniform("uMaximumWaveAngularGradient");
    uMaximumAmplitudeGradient_ = uniform("uMaximumAmplitudeGradient");
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
    if (resources.vao == 0 || program_ == 0 || !gl_) return;
    if (ctx.graph.scene.isContributorMode() || ctx.graph.scene.model().cells().empty()) return;

    const HexSphereModel& model = ctx.graph.scene.model();
    const WaterParams& water = ctx.graph.scene.resolvedWaterParamsValue();
    const ResolvedWaterWaveSpec& wave = ctx.graph.scene.resolvedWaterWaveSpec();
    const WaterWaveUniformData waveData = WaterWaveUniformData::gather(wave, ctx.lighting.waterTime);

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

    uploadWaterFrameUniforms(
        gl_,
        commonLocations_,
        ctx.mvp,
        ctx.lighting.direction,
        ctx.cameraPos,
        ctx.mvp,
        ctx.invViewProjection,
        ctx.viewportSize,
        model.waterSurfaceRadius(),
        water);
    uploadWaterWaveUniforms(gl_, commonLocations_, wave, waveData);
    gl_->glUniform1i(uWavesEnabled_, water.wavesEnabled ? 1 : 0);

    // Параметры морской оболочки: нужны только ray-marching.
    gl_->glUniform1f(uWaterShellInnerRadius_, wave.bounds.innerRadius);
    gl_->glUniform1f(uWaterShellOuterRadius_, wave.bounds.outerRadius);
    gl_->glUniform1f(uBottomEpsilon_, wave.bounds.bottomEpsilon);
    gl_->glUniform1f(uBoundEpsilon_, wave.bounds.boundEpsilon);
    gl_->glUniform1f(uHitEpsilon_, std::max(wave.bounds.boundEpsilon * 0.25f, 1e-6f));
    gl_->glUniform1f(uTerrainContactEpsilon_, std::max(wave.bounds.bottomEpsilon, 1e-5f));
    gl_->glUniform1f(uMaximumPhaseGradient_, wave.maximumPhaseGradient);
    gl_->glUniform1f(uMaximumWaveAngularGradient_, wave.maximumWaveAngularGradient);
    gl_->glUniform1f(uMaximumAmplitudeGradient_, wave.maximumAmplitudeAngularGradient);

    auto bindTexture = [&](GLenum unit, GLenum target, GLuint texture, GLint sampler) {
        gl_->glActiveTexture(unit);
        gl_->glBindTexture(target, texture);
        gl_->glUniform1i(sampler, static_cast<GLint>(unit - GL_TEXTURE0));
        };
    bindTexture(GL_TEXTURE0, GL_TEXTURE_CUBE_MAP, resources.envCubemap, commonLocations_.envMap);
    bindTexture(GL_TEXTURE1, GL_TEXTURE_2D, resources.sceneDepthTexture, uSceneDepthTexture_);
    bindTexture(GL_TEXTURE2, GL_TEXTURE_CUBE_MAP, resources.planetRadiusAtlas, uPlanetRadiusAtlas_);
    bindTexture(GL_TEXTURE3, GL_TEXTURE_CUBE_MAP, resources.planetSurfaceKindAtlas, uPlanetSurfaceKindAtlas_);
    bindTexture(GL_TEXTURE4, GL_TEXTURE_CUBE_MAP, resources.planetShoreDistanceAtlas, uPlanetShoreDistanceAtlas_);

    gl_->glEnable(GL_BLEND);
    gl_->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // Coverage is independent of mesh winding and the camera position.
    gl_->glDisable(GL_CULL_FACE);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glDepthFunc(GL_LEQUAL);
    gl_->glDepthMask(GL_TRUE);

    gl_->glBindVertexArray(resources.vao);
    // The fragment shader writes the depth of the traced wave surface.
    gl_->glDrawArrays(GL_TRIANGLES, 0, 3);
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
