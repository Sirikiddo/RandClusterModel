#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QtOpenGL>

#include "renderers/HexSphereRenderer.h"

class TerrainRenderer {
public:
    TerrainRenderer(QOpenGLFunctions_3_3_Core* gl,
                    GLuint program,
                    GLint uMvp,
                    GLint uModel,
                    GLint uLightDir,
                    GLuint vao,
                    const GLsizei& indexCount);

    void render(const HexSphereRenderer::RenderContext& ctx) const;

private:
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint program_ = 0;
    GLint uMvp_ = -1;
    GLint uModel_ = -1;
    GLint uLightDir_ = -1;
    GLuint vao_ = 0;
    const GLsizei& indexCount_;
};

