#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QtOpenGL>
#include <memory>
#include <unordered_map>

#include "renderers/HexSphereRenderer.h"
#include "model/CarModelHandler.h"
#include "model/FactoryModelHandler.h"
#include "model/MineModelHandler.h"

class EntityRenderer {
public:
    ~EntityRenderer();

    EntityRenderer(QOpenGLFunctions_3_3_Core* gl,
        GLuint progWire,
        GLuint progSel,
        GLuint progModel,
        GLuint progFactory,
        GLuint progSteam,
        GLint uMvpWire,
        GLint uMvpSel,
        GLint uMvpModel,
        GLint uModel,
        GLint uLightDir,
        GLint uViewPos,
        GLint uColor,
        GLint uUseTexture,
        GLint uMvpFactory,
        GLint uModelFactory,
        GLint uLightDirFactory,
        GLint uViewPosFactory,
        GLint uColorFactory,
        GLint uUseTextureFactory,
        GLint uMvpSteam,
        GLint uModelSteam,
        GLint uTimeSteam,
        GLint uViewPosSteam,
        GLuint vaoPyramid,
        const GLsizei& pyramidVertexCount,
        const std::shared_ptr<ModelHandler>& treeModel,
        const std::shared_ptr<ModelHandler>& firTreeModel,
        const std::shared_ptr<CarModelHandler>& carModel,
        const std::shared_ptr<FactoryModelHandler>& factoryModel,
        const std::shared_ptr<MineModelHandler>& mineModel);

    void renderEntities(const HexSphereRenderer::RenderContext& ctx) const;
    void renderTrees(const HexSphereRenderer::RenderContext& ctx) const;
    void renderCar(const HexSphereRenderer::RenderContext& ctx, const ecs::Entity& entity) const;
    void renderFactory(const HexSphereRenderer::RenderContext& ctx, const ecs::Entity& entity) const;
    void renderMine(const HexSphereRenderer::RenderContext& ctx, const ecs::Entity& entity) const;
    void initializeSteamResources();

private:
    struct SteamVertex {
        QVector3D emitter;
        float seed = 0.0f;
    };

    struct WheelAnimationState {
        QVector3D lastWorldPosition{ 0.0f, 0.0f, 0.0f };
        float spinDegrees = 0.0f;
        bool initialized = false;
    };

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint progWire_ = 0;
    GLuint progSel_ = 0;
    GLuint progModel_ = 0;
    GLuint progFactory_ = 0;
    GLuint progSteam_ = 0;
    GLint uMvpWire_ = -1;
    GLint uMvpSel_ = -1;
    GLint uMvpModel_ = -1;
    GLint uModel_ = -1;
    GLint uLightDir_ = -1;
    GLint uViewPos_ = -1;
    GLint uColor_ = -1;
    GLint uUseTexture_ = -1;
    GLint uMvpFactory_ = -1;
    GLint uModelFactory_ = -1;
    GLint uLightDirFactory_ = -1;
    GLint uViewPosFactory_ = -1;
    GLint uColorFactory_ = -1;
    GLint uUseTextureFactory_ = -1;
    GLint uMvpSteam_ = -1;
    GLint uModelSteam_ = -1;
    GLint uTimeSteam_ = -1;
    GLint uViewPosSteam_ = -1;
    GLuint vaoPyramid_ = 0;
    const GLsizei& pyramidVertexCount_;
    GLuint steamVao_ = 0;
    GLuint steamVbo_ = 0;
    GLsizei steamParticleCount_ = 0;

    std::shared_ptr<ModelHandler> treeModel_;
    std::shared_ptr<ModelHandler> firTreeModel_;
    std::shared_ptr<CarModelHandler> carModel_;
    std::shared_ptr<FactoryModelHandler> factoryModel_;
    std::shared_ptr<MineModelHandler> mineModel_;
    mutable std::unordered_map<ecs::EntityId, WheelAnimationState> wheelAnimationStates_;
};
