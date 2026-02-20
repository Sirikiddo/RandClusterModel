#include "renderers/TerrainRenderer.h"

#include <QOpenGLContext>
#include <QtDebug>

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
    if (indexCount_ == 0 || program_ == 0) return;

    QOpenGLContext* currentCtx = QOpenGLContext::currentContext();
    if (!currentCtx) {
        qCritical() << "[TerrainRenderer] No current GL context before draw"
                    << "program=" << program_
                    << "vao=" << vao_
                    << "indexCount=" << indexCount_;
        return;
    }

    gl_->glUseProgram(program_);
    gl_->glUniformMatrix4fv(uMvp_, 1, GL_FALSE, ctx.mvp.constData());
    QMatrix4x4 model; model.setToIdentity();
    gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());
    const QVector3D& lightDir = ctx.lighting.direction;
    gl_->glUniform3f(uLightDir_, lightDir.x(), lightDir.y(), lightDir.z());
    gl_->glBindVertexArray(vao_);

    GLint boundVao = 0;
    GLint boundEbo = 0;
    gl_->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVao);
    gl_->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundEbo);
    if (boundVao != GLint(vao_) || boundEbo == 0) {
        qCritical() << "[TerrainRenderer] Invalid draw state"
                    << "expectedVao=" << vao_
                    << "boundVao=" << boundVao
                    << "boundEbo=" << boundEbo
                    << "indexCount=" << indexCount_;
    }

    gl_->glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);

    const GLenum err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        qCritical() << "[TerrainRenderer] glDrawElements error"
                    << Qt::hex << int(err) << Qt::dec
                    << "boundVao=" << boundVao
                    << "boundEbo=" << boundEbo
                    << "indexCount=" << indexCount_;
    }

    gl_->glBindVertexArray(0);
}

