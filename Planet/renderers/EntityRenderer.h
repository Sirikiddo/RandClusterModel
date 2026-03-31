// EntityRenderer.h
#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QtOpenGL>
#include <memory>

#include "renderers/HexSphereRenderer.h"
#include "model/CarModelHandler.h"

class EntityRenderer {
public:
    // ÎÁÍÎÂËÅÍÍÛÉ ÊÎÍÑÒĞÓÊÒÎĞ - òåïåğü ñ äâóìÿ ìîäåëÿìè äåğåâüåâ
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
        const std::shared_ptr<ModelHandler>& treeModel,
        const std::shared_ptr<ModelHandler>& firTreeModel,  // ÄÎÁÀÂËßÅÌ
        const std::shared_ptr<CarModelHandler>& carModel);

    void renderEntities(const HexSphereRenderer::RenderContext& ctx) const;
    void renderTrees(const HexSphereRenderer::RenderContext& ctx) const;
    void renderCar(const HexSphereRenderer::RenderContext& ctx, const ecs::Entity& entity) const;

private:
    struct WheelAnimationState {
        QVector3D lastWorldPosition{ 0.0f, 0.0f, 0.0f };
        float spinDegrees = 0.0f;
        bool initialized = false;
    };
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

    // ÄÂÅ ÌÎÄÅËÈ ÄÅĞÅÂÜÅÂ
    std::shared_ptr<ModelHandler> treeModel_;      // Îáû÷íûå äåğåâüÿ
    std::shared_ptr<ModelHandler> firTreeModel_;   // ¨ëî÷êè

    std::shared_ptr<CarModelHandler> carModel_;
    mutable std::unordered_map<ecs::EntityId, WheelAnimationState> wheelAnimationStates_;
};