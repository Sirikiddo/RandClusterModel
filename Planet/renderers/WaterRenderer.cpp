#include "renderers/WaterRenderer.h"

WaterRenderer::WaterRenderer(QOpenGLFunctions_3_3_Core* gl,
                             GLuint program,
                             GLint uMvp,
                             GLint uTime,
                             GLint uLightDir,
                             GLint uViewPos,
                             GLint uEnvMap,
                             GLuint& envCubemap,
                             GLuint vao,
                             const GLsizei& indexCount)
    : gl_(gl)
    , program_(program)
    , uMvp_(uMvp)
    , uTime_(uTime)
    , uLightDir_(uLightDir)
    , uViewPos_(uViewPos)
    , uEnvMap_(uEnvMap)
    , envCubemap_(envCubemap)
    , vao_(vao)
    , indexCount_(indexCount) {}

void WaterRenderer::render(const HexSphereRenderer::RenderContext& ctx) const {
    if (indexCount_ == 0 || program_ == 0) return;

    gl_->glUseProgram(program_);
    gl_->glUniformMatrix4fv(uMvp_, 1, GL_FALSE, ctx.mvp.constData());
    gl_->glUniform1f(uTime_, ctx.lighting.waterTime);
    const QVector3D& lightDir = ctx.lighting.direction;
    gl_->glUniform3f(uLightDir_, lightDir.x(), lightDir.y(), lightDir.z());
    gl_->glUniform3f(uViewPos_, ctx.cameraPos.x(), ctx.cameraPos.y(), ctx.cameraPos.z());
    if (uEnvMap_ != -1 && envCubemap_ != 0) {
        gl_->glActiveTexture(GL_TEXTURE0);
        gl_->glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);
        gl_->glUniform1i(uEnvMap_, 0);
    }

    gl_->glEnable(GL_BLEND);
    gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl_->glDepthMask(GL_FALSE);
    gl_->glBindVertexArray(vao_);
    gl_->glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    gl_->glBindVertexArray(0);
    gl_->glDepthMask(GL_TRUE);
    gl_->glDisable(GL_BLEND);
}

