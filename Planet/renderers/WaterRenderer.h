#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QtOpenGL>

#include "renderers/HexSphereRenderer.h"

class WaterRenderer {
public:
    WaterRenderer(QOpenGLFunctions_3_3_Core* gl,
                  GLuint program,
                  GLint uMvp,
                  GLint uTime,
                  GLint uLightDir,
                  GLint uViewPos,
                  GLint uEnvMap,
                  GLuint& envCubemap,
                  GLuint vao,
                  const GLsizei& indexCount);

    void render(const HexSphereRenderer::RenderContext& ctx) const;

private:
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint program_ = 0;
    GLint uMvp_ = -1;
    GLint uTime_ = -1;
    GLint uLightDir_ = -1;
    GLint uViewPos_ = -1;
    GLint uEnvMap_ = -1;
    GLuint& envCubemap_;
    GLuint vao_ = 0;
    const GLsizei& indexCount_;
};

