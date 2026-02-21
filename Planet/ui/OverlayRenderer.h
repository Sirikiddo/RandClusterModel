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
                    GLsizei lineVertexCount,
                    GLsizei selLineVertexCount,
                    GLsizei pathVertexCount);

    void render(const HexSphereRenderer::RenderContext& ctx) const;

    void updatePrograms(GLuint progWire, GLuint progSel, GLint uMvpWire, GLint uMvpSel) {
        progWire_ = progWire;
        progSel_ = progSel;
        uMvpWire_ = uMvpWire;
        uMvpSel_ = uMvpSel;
    }

    void updateCounts(GLsizei lineCount, GLsizei selCount, GLsizei pathCount) {
        lineVertexCount_ = lineCount;
        selLineVertexCount_ = selCount;
        pathVertexCount_ = pathCount;
    }

private:
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint progWire_ = 0;
    GLuint progSel_ = 0;
    GLint uMvpWire_ = -1;
    GLint uMvpSel_ = -1;
    GLuint vaoWire_ = 0;
    GLuint vaoSel_ = 0;
    GLuint vaoPath_ = 0;
    GLsizei lineVertexCount_;
    GLsizei selLineVertexCount_;
    GLsizei pathVertexCount_;
};

