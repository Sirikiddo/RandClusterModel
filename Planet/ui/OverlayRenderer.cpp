#include "ui/OverlayRenderer.h"

OverlayRenderer::OverlayRenderer(QOpenGLFunctions_3_3_Core* gl,
                                 GLuint progWire,
                                 GLuint progSel,
                                 GLint uMvpWire,
                                 GLint uMvpSel,
                                 GLuint vaoWire,
                                 GLuint vaoSel,
                                 GLuint vaoPath,
                                 const GLsizei& lineVertexCount,
                                 const GLsizei& selLineVertexCount,
                                 const GLsizei& pathVertexCount)
    : gl_(gl)
    , progWire_(progWire)
    , progSel_(progSel)
    , uMvpWire_(uMvpWire)
    , uMvpSel_(uMvpSel)
    , vaoWire_(vaoWire)
    , vaoSel_(vaoSel)
    , vaoPath_(vaoPath)
    , lineVertexCount_(lineVertexCount)
    , selLineVertexCount_(selLineVertexCount)
    , pathVertexCount_(pathVertexCount) {}

void OverlayRenderer::render(const HexSphereRenderer::RenderContext& ctx) const {
    if (selLineVertexCount_ > 0 && progSel_) {
        gl_->glUseProgram(progSel_);
        gl_->glUniformMatrix4fv(uMvpSel_, 1, GL_FALSE, ctx.mvp.constData());
        gl_->glBindVertexArray(vaoSel_);
        gl_->glDrawArrays(GL_LINES, 0, selLineVertexCount_);
    }
    if (lineVertexCount_ > 0 && progWire_) {
        gl_->glUseProgram(progWire_);
        gl_->glUniformMatrix4fv(uMvpWire_, 1, GL_FALSE, ctx.mvp.constData());
        gl_->glBindVertexArray(vaoWire_);
        gl_->glDrawArrays(GL_LINES, 0, lineVertexCount_);
    }
    if (pathVertexCount_ > 0 && progWire_) {
        gl_->glUseProgram(progWire_);
        gl_->glUniformMatrix4fv(uMvpWire_, 1, GL_FALSE, ctx.mvp.constData());
        gl_->glBindVertexArray(vaoPath_);
        gl_->glDrawArrays(GL_LINE_STRIP, 0, pathVertexCount_);
    }
}

