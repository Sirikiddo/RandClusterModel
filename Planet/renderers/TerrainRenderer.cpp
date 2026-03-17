#include "renderers/TerrainRenderer.h"
#include <QDebug>

TerrainRenderer::TerrainRenderer(QOpenGLFunctions_3_3_Core* gl,
    GLuint program,
    GLint uMvp,
    GLint uModel,
    GLint uLightDir,
    GLint uNormalMatrix,
    GLuint vao)
    : gl_(gl)
    , program_(program)
    , uMvp_(uMvp)
    , uModel_(uModel)
    , uLightDir_(uLightDir)
    , uNormalMatrix_(uNormalMatrix)
    , vao_(vao) {
}

void TerrainRenderer::render(const HexSphereRenderer::RenderContext& ctx, GLsizei indexCount) const {
    if (indexCount == 0 || program_ == 0 || vao_ == 0) {
        return;
    }

    gl_->glUseProgram(program_);
    gl_->glUniformMatrix4fv(uMvp_, 1, GL_FALSE, ctx.mvp.constData());

    QMatrix4x4 model;
    model.setToIdentity();
    gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());

    gl_->glUniformMatrix3fv(uNormalMatrix_, 1, GL_FALSE, model.normalMatrix().constData());

    const QVector3D& lightDir = ctx.lighting.direction;
    gl_->glUniform3f(uLightDir_, lightDir.x(), lightDir.y(), lightDir.z());

    gl_->glBindVertexArray(vao_);

    GLenum err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        qDebug() << "OpenGL error ignored before draw:" << err;
    }

    gl_->glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);

    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        qDebug() << "OpenGL error ignored after draw:" << err;
    }

    gl_->glBindVertexArray(0);
}