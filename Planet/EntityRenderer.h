#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QtOpenGL>
#include <memory>

#include "HexSphereRenderer.h"

class EntityRenderer {
public:
    EntityRenderer(QOpenGLFunctions_3_3_Core* gl,
                   GLuint progWire,
                   GLuint progSel,
                   GLuint progModel,
                   GLint uMvpWire,
                   GLint uMvpSel,
                   GLint uMvpModel,
                   GLint uModel,
                   GLint uLightDir,
                   GLint uViewPos,
                   GLint uColor,
                   GLint uUseTexture,
                   GLuint vaoPyramid,
                   const GLsizei& pyramidVertexCount,
                   const std::shared_ptr<ModelHandler>& treeModel);

    void renderEntities(const HexSphereRenderer::RenderContext& ctx) const;
    void renderTrees(const HexSphereRenderer::RenderContext& ctx) const;

private:
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint progWire_ = 0;
    GLuint progSel_ = 0;
    GLuint progModel_ = 0;
    GLint uMvpWire_ = -1;
    GLint uMvpSel_ = -1;
    GLint uMvpModel_ = -1;
    GLint uModel_ = -1;
    GLint uLightDir_ = -1;
    GLint uViewPos_ = -1;
    GLint uColor_ = -1;
    GLint uUseTexture_ = -1;
    GLuint vaoPyramid_ = 0;
    const GLsizei& pyramidVertexCount_;
    std::shared_ptr<ModelHandler> treeModel_;
};

