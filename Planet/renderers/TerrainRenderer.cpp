#include "renderers/TerrainRenderer.h"
#include "../DebugMacros.h"

TerrainRenderer::TerrainRenderer(QOpenGLFunctions_3_3_Core* gl,
                                 GLuint program,
                                 GLint uMvp,
                                 GLint uModel,
                                 GLint uLightDir,
                                 GLuint vao,
                                 const GLsizei& indexCount)
    : gl_(gl)
    , program_(program)
    , uMvp_(uMvp)
    , uModel_(uModel)
    , uLightDir_(uLightDir)
    , vao_(vao)
    , indexCount_(indexCount) {}

void TerrainRenderer::render(const HexSphereRenderer::RenderContext& ctx) const {
    DEBUG_CALL_PARAM("indexCount=" << indexCount_ << "program=" << program_);
    if (indexCount_ == 0 || program_ == 0) {
        DEBUG_CALL_PARAM("SKIP: no data");
        return;
    }

    gl_->glUseProgram(program_);
    gl_->glUniformMatrix4fv(uMvp_, 1, GL_FALSE, ctx.mvp.constData());
    QMatrix4x4 model; model.setToIdentity();
    gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());
    const QVector3D& lightDir = ctx.lighting.direction;
    gl_->glUniform3f(uLightDir_, lightDir.x(), lightDir.y(), lightDir.z());
    gl_->glBindVertexArray(vao_);
    DEBUG_CALL_PARAM("calling glDrawElements with " << indexCount_ << " indices");
    gl_->glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);

    // Проверка ошибок после отрисовки
    GLenum err;
    while ((err = gl_->glGetError()) != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after draw: " << err);
    }

    gl_->glBindVertexArray(0);
}

