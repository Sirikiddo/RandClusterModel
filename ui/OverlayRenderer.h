#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QtOpenGL>

#include "renderers/HexSphereRenderer.h"

class OverlayRenderer {
public:
    OverlayRenderer(QOpenGLFunctions_3_3_Core* gl,
                    GLuint progWire,
                    GLuint progSel,
                    GLint uMvpWire,
                    GLint uMvpSel,
                    GLuint vaoWire,
                    GLuint vaoSel,
                    GLuint vaoPath,
                    const GLsizei& lineVertexCount,
                    const GLsizei& selLineVertexCount,
                    const GLsizei& pathVertexCount);

    void render(const HexSphereRenderer::RenderContext& ctx) const;

private:
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint progWire_ = 0;
    GLuint progSel_ = 0;
    GLint uMvpWire_ = -1;
    GLint uMvpSel_ = -1;
    GLuint vaoWire_ = 0;
    GLuint vaoSel_ = 0;
    GLuint vaoPath_ = 0;
    const GLsizei& lineVertexCount_;
    const GLsizei& selLineVertexCount_;
    const GLsizei& pathVertexCount_;
};

