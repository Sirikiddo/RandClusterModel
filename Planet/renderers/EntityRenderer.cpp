#include "renderers/EntityRenderer.h"

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QtDebug>
#include <cstddef>
#include <cmath>
#include <random>

#include "model/SurfacePlacement.h"

namespace {
    QMatrix4x4 basisFromHorizontalForward(const QVector3D& unitUp, QVector3D forwardTangent) {
        forwardTangent = forwardTangent - QVector3D::dotProduct(forwardTangent, unitUp) * unitUp;
        if (forwardTangent.length() < 1e-4f) {
            QVector3D refX(1, 0, 0);
            QVector3D right = refX - QVector3D::dotProduct(refX, unitUp) * unitUp;
            if (right.length() < 0.01f) {
                refX = QVector3D(0, 0, 1);
                right = refX - QVector3D::dotProduct(refX, unitUp) * unitUp;
            }
            right.normalize();
            forwardTangent = QVector3D::crossProduct(unitUp, right).normalized();
        }
        else {
            forwardTangent.normalize();
        }

        const QVector3D right = QVector3D::crossProduct(forwardTangent, unitUp).normalized();

        QMatrix4x4 rotation;
        rotation.setColumn(0, QVector4D(right, 0.0f));
        rotation.setColumn(1, QVector4D(unitUp, 0.0f));
        rotation.setColumn(2, QVector4D(forwardTangent, 0.0f));
        return rotation;
    }

    void orientToSurfaceNormal(QMatrix4x4& matrix, const QVector3D& normal) {
        const QVector3D up = normal.normalized();
        const QVector3D seedForward = (qAbs(QVector3D::dotProduct(up, QVector3D(0, 0, 1))) > 0.99f)
            ? QVector3D(1, 0, 0)
            : QVector3D(0, 0, 1);
        matrix = matrix * basisFromHorizontalForward(up, seedForward);
    }
}

EntityRenderer::~EntityRenderer() {
    if (!gl_ || !QOpenGLContext::currentContext()) {
        return;
    }
    if (steamVao_ != 0) {
        gl_->glDeleteVertexArrays(1, &steamVao_);
    }
    if (steamVbo_ != 0) {
        gl_->glDeleteBuffers(1, &steamVbo_);
    }
}

EntityRenderer::EntityRenderer(QOpenGLFunctions_3_3_Core* gl,
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
    const std::shared_ptr<MineModelHandler>& mineModel)
    : gl_(gl)
    , progWire_(progWire)
    , progSel_(progSel)
    , progModel_(progModel)
    , progFactory_(progFactory)
    , progSteam_(progSteam)
    , uMvpWire_(uMvpWire)
    , uMvpSel_(uMvpSel)
    , uMvpModel_(uMvpModel)
    , uModel_(uModel)
    , uLightDir_(uLightDir)
    , uViewPos_(uViewPos)
    , uColor_(uColor)
    , uUseTexture_(uUseTexture)
    , uMvpFactory_(uMvpFactory)
    , uModelFactory_(uModelFactory)
    , uLightDirFactory_(uLightDirFactory)
    , uViewPosFactory_(uViewPosFactory)
    , uColorFactory_(uColorFactory)
    , uUseTextureFactory_(uUseTextureFactory)
    , uMvpSteam_(uMvpSteam)
    , uModelSteam_(uModelSteam)
    , uTimeSteam_(uTimeSteam)
    , uViewPosSteam_(uViewPosSteam)
    , vaoPyramid_(vaoPyramid)
    , pyramidVertexCount_(pyramidVertexCount)
    , treeModel_(treeModel)
    , firTreeModel_(firTreeModel)
    , carModel_(carModel)
    , factoryModel_(factoryModel)
    , mineModel_(mineModel) {
}

void EntityRenderer::initializeSteamResources() {
    if (!gl_ || steamVao_ != 0 || !QOpenGLContext::currentContext()) {
        return;
    }

    static const QVector3D emitters[] = {
        QVector3D(6.281f, 8.982f, 5.966f)
    };

    std::vector<SteamVertex> particles;
    particles.reserve(16);
    for (int emitterIndex = 0; emitterIndex < 1; ++emitterIndex) {
        for (int i = 0; i < 16; ++i) {
            SteamVertex v;
            v.emitter = emitters[emitterIndex];
            v.seed = (static_cast<float>(emitterIndex) * 0.37f) + (static_cast<float>(i) / 16.0f);
            particles.push_back(v);
        }
    }

    steamParticleCount_ = static_cast<GLsizei>(particles.size());

    gl_->glGenVertexArrays(1, &steamVao_);
    gl_->glGenBuffers(1, &steamVbo_);
    gl_->glBindVertexArray(steamVao_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, steamVbo_);
    gl_->glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(particles.size() * sizeof(SteamVertex)),
        particles.data(),
        GL_STATIC_DRAW);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SteamVertex), reinterpret_cast<void*>(offsetof(SteamVertex, emitter)));
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(SteamVertex), reinterpret_cast<void*>(offsetof(SteamVertex, seed)));
    gl_->glEnableVertexAttribArray(1);
    gl_->glBindVertexArray(0);
}

void EntityRenderer::renderEntities(const HexSphereRenderer::RenderContext& ctx) const {
    ctx.graph.ecs.each<ecs::Mesh, ecs::Transform>([&](const ecs::Entity& e, const ecs::Mesh& mesh, const ecs::Transform&) {
        if (mesh.meshId == "factory") {
            renderFactory(ctx, e);
        }
        else if (mesh.meshId == "mine") {
            renderMine(ctx, e);
        }
        else {
            renderCar(ctx, e);
        }
    });
}

void EntityRenderer::renderCar(const HexSphereRenderer::RenderContext& ctx, const ecs::Entity& entity) const {
    if (!carModel_ || !carModel_->isReady()) return;

    QVector3D surfacePos;
    if (entity.currentCell >= 0) {
        surfacePos = computeSurfacePoint(ctx.graph.scene, entity.currentCell, ctx.graph.heightStep, 0.0f);
    }
    else {
        auto* transform = ctx.graph.ecs.get<ecs::Transform>(entity.id);
        if (transform) surfacePos = transform->position;
        else return;
    }

    const float heightOffset = 0.025f;
    QVector3D elevatedPos = surfacePos.normalized() * (surfacePos.length() + heightOffset);

    const GLboolean depthTestWasEnabled = gl_->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = gl_->glIsEnabled(GL_BLEND);
    const GLboolean cullWasEnabled = gl_->glIsEnabled(GL_CULL_FACE);
    GLboolean depthWriteMask = GL_TRUE;
    gl_->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);

    gl_->glDisable(GL_BLEND);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glDepthMask(GL_TRUE);
    gl_->glDisable(GL_CULL_FACE);

    QMatrix4x4 model;
    model.translate(elevatedPos);

    const QVector3D up = elevatedPos.normalized();

    auto* anim = ctx.graph.ecs.get<ecs::Animation>(entity.id);
    auto* transform = ctx.graph.ecs.get<ecs::Transform>(entity.id);

    QVector3D forwardTangent(0, 0, 0);
    if (anim && anim->type == ecs::Animation::Type::MoveTo && anim->surfaceForward.length() > 0.5f) {
        forwardTangent = anim->surfaceForward;
    }
    else if (transform && transform->surfaceForward.length() > 0.5f) {
        forwardTangent = transform->surfaceForward;
    }

    model = model * basisFromHorizontalForward(up, forwardTangent);
    model = model * carModel_->localAlignment();

    const float carScale = 0.035f;
    model.scale(carScale);

    if (entity.selected) {
        model.scale(1.2f);
    }

    const QMatrix4x4 mvpCar = ctx.mvp * model;

    gl_->glUseProgram(progModel_);

    const GLint uIsCar = gl_->glGetUniformLocation(progModel_, "uIsCar");
    if (uIsCar >= 0) gl_->glUniform1i(uIsCar, 1);
    const GLint uUseFoliageColor = gl_->glGetUniformLocation(progModel_, "uUseFoliageColor");
    if (uUseFoliageColor >= 0) gl_->glUniform1i(uUseFoliageColor, 0);
    const GLint uWindTime = gl_->glGetUniformLocation(progModel_, "uWindTime");
    if (uWindTime >= 0) gl_->glUniform1f(uWindTime, 0.0f);

    gl_->glUniformMatrix4fv(uMvpModel_, 1, GL_FALSE, mvpCar.constData());
    gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());

    QVector3D eye = (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();
    gl_->glUniform3f(uViewPos_, eye.x(), eye.y(), eye.z());
    gl_->glUniform3f(uLightDir_, 0.5f, 1.0f, 0.3f);
    gl_->glUniform3f(uColor_, 1.0f, 1.0f, 1.0f);
    gl_->glUniform1i(uUseTexture_, 1);

    float wheelSpinDegrees = 0.0f;
    if (const float localWheelRadius = carModel_->wheelRadius(); localWheelRadius > 1e-5f) {
        auto& wheelState = wheelAnimationStates_[entity.id];
        if (wheelState.initialized) {
            const QVector3D worldDelta = elevatedPos - wheelState.lastWorldPosition;
            float signedDistance = worldDelta.length();
            if (forwardTangent.length() > 1e-4f) {
                signedDistance = QVector3D::dotProduct(worldDelta, forwardTangent.normalized());
            }

            const float wheelScale = entity.selected ? (carScale * 1.2f) : carScale;
            const float worldWheelRadius = localWheelRadius * wheelScale;
            if (worldWheelRadius > 1e-5f) {
                constexpr float kRadiansToDegrees = 57.2957795f;
                wheelState.spinDegrees += -signedDistance / worldWheelRadius * kRadiansToDegrees;
                wheelState.spinDegrees = std::fmod(wheelState.spinDegrees, 360.0f);
            }
        }

        wheelState.lastWorldPosition = elevatedPos;
        wheelState.initialized = true;
        wheelSpinDegrees = wheelState.spinDegrees;
    }

    carModel_->draw(progModel_, mvpCar, model, ctx.camera.view, wheelSpinDegrees);

    if (blendWasEnabled) gl_->glEnable(GL_BLEND);
    else gl_->glDisable(GL_BLEND);

    if (cullWasEnabled) gl_->glEnable(GL_CULL_FACE);
    else gl_->glDisable(GL_CULL_FACE);

    if (depthTestWasEnabled) gl_->glEnable(GL_DEPTH_TEST);
    else gl_->glDisable(GL_DEPTH_TEST);

    gl_->glDepthMask(depthWriteMask);
}

void EntityRenderer::renderFactory(const HexSphereRenderer::RenderContext& ctx, const ecs::Entity& entity) const {
    if (!factoryModel_ || !factoryModel_->isReady() || progFactory_ == 0) return;
    if (steamVao_ == 0) {
        const_cast<EntityRenderer*>(this)->initializeSteamResources();
    }

    QVector3D surfacePos;
    if (entity.currentCell >= 0) {
        surfacePos = computeSurfacePoint(ctx.graph.scene, entity.currentCell, ctx.graph.heightStep, 0.0f);
    }
    else {
        auto* transform = ctx.graph.ecs.get<ecs::Transform>(entity.id);
        if (transform) surfacePos = transform->position;
        else return;
    }

    const float heightOffset = -0.04f;
    QVector3D elevatedPos = surfacePos.normalized() * (surfacePos.length() + heightOffset);

    const GLboolean depthTestWasEnabled = gl_->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = gl_->glIsEnabled(GL_BLEND);
    const GLboolean cullWasEnabled = gl_->glIsEnabled(GL_CULL_FACE);
    GLboolean depthWriteMask = GL_TRUE;
    gl_->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);

    gl_->glDisable(GL_BLEND);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glDepthMask(GL_TRUE);
    gl_->glDisable(GL_CULL_FACE);

    QMatrix4x4 model;
    model.translate(elevatedPos);

    auto* transform = ctx.graph.ecs.get<ecs::Transform>(entity.id);
    QVector3D forwardTangent(0, 0, 0);
    if (transform && transform->surfaceForward.length() > 0.5f) {
        forwardTangent = transform->surfaceForward;
    }

    model = model * basisFromHorizontalForward(elevatedPos.normalized(), forwardTangent);
    model.scale(0.008f);
    model = model * factoryModel_->localPlacement();

    if (entity.selected) {
        model.scale(1.08f);
    }

    const QMatrix4x4 mvpFactory = ctx.mvp * model;
    const QVector3D eye = (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

    gl_->glUseProgram(progFactory_);
    gl_->glUniformMatrix4fv(uMvpFactory_, 1, GL_FALSE, mvpFactory.constData());
    gl_->glUniformMatrix4fv(uModelFactory_, 1, GL_FALSE, model.constData());
    gl_->glUniform3f(uViewPosFactory_, eye.x(), eye.y(), eye.z());
    gl_->glUniform3f(uLightDirFactory_, 0.35f, 1.0f, 0.2f);
    gl_->glUniform3f(uColorFactory_, 1.0f, 1.0f, 1.0f);
    gl_->glUniform1i(uUseTextureFactory_, 1);

    factoryModel_->draw(progFactory_, mvpFactory, model, ctx.camera.view);

    if (progSteam_ != 0 && steamVao_ != 0 && steamParticleCount_ > 0) {
        static QElapsedTimer steamTimer;
        if (!steamTimer.isValid()) {
            steamTimer.start();
        }
        const float steamTimeSeconds = steamTimer.elapsed() / 1000.0f;

        gl_->glEnable(GL_BLEND);
        gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl_->glDepthMask(GL_FALSE);
        gl_->glEnable(GL_PROGRAM_POINT_SIZE);

        gl_->glUseProgram(progSteam_);
        gl_->glUniformMatrix4fv(uMvpSteam_, 1, GL_FALSE, mvpFactory.constData());
        gl_->glUniformMatrix4fv(uModelSteam_, 1, GL_FALSE, model.constData());
        gl_->glUniform1f(uTimeSteam_, steamTimeSeconds);
        gl_->glUniform3f(uViewPosSteam_, eye.x(), eye.y(), eye.z());

        gl_->glBindVertexArray(steamVao_);
        gl_->glDrawArrays(GL_POINTS, 0, steamParticleCount_);
        gl_->glBindVertexArray(0);
        gl_->glDisable(GL_PROGRAM_POINT_SIZE);
    }

    if (blendWasEnabled) gl_->glEnable(GL_BLEND);
    else gl_->glDisable(GL_BLEND);

    if (cullWasEnabled) gl_->glEnable(GL_CULL_FACE);
    else gl_->glDisable(GL_CULL_FACE);

    if (depthTestWasEnabled) gl_->glEnable(GL_DEPTH_TEST);
    else gl_->glDisable(GL_DEPTH_TEST);

    gl_->glDepthMask(depthWriteMask);
}

void EntityRenderer::renderMine(const HexSphereRenderer::RenderContext& ctx, const ecs::Entity& entity) const {
    if (!mineModel_ || !mineModel_->isReady() || progFactory_ == 0) return;

    QVector3D surfacePos;
    if (entity.currentCell >= 0) {
        surfacePos = computeSurfacePoint(ctx.graph.scene, entity.currentCell, ctx.graph.heightStep, 0.0f);
    }
    else {
        auto* transform = ctx.graph.ecs.get<ecs::Transform>(entity.id);
        if (transform) surfacePos = transform->position;
        else return;
    }

    const float heightOffset = -0.005f;
    QVector3D elevatedPos = surfacePos.normalized() * (surfacePos.length() + heightOffset);

    const GLboolean depthTestWasEnabled = gl_->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = gl_->glIsEnabled(GL_BLEND);
    const GLboolean cullWasEnabled = gl_->glIsEnabled(GL_CULL_FACE);
    GLboolean depthWriteMask = GL_TRUE;
    gl_->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);

    gl_->glDisable(GL_BLEND);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glDepthMask(GL_TRUE);
    gl_->glDisable(GL_CULL_FACE);

    QMatrix4x4 model;
    model.translate(elevatedPos);

    auto* transform = ctx.graph.ecs.get<ecs::Transform>(entity.id);
    QVector3D forwardTangent(0, 0, 0);
    if (transform && transform->surfaceForward.length() > 0.5f) {
        forwardTangent = transform->surfaceForward;
    }

    model = model * basisFromHorizontalForward(elevatedPos.normalized(), forwardTangent);
    model.scale(0.008f);
    model = model * mineModel_->localPlacement();

    if (entity.selected) {
        model.scale(1.08f);
    }

    const QMatrix4x4 mvpMine = ctx.mvp * model;

    gl_->glUseProgram(progFactory_);
    gl_->glUniformMatrix4fv(uMvpFactory_, 1, GL_FALSE, mvpMine.constData());
    gl_->glUniformMatrix4fv(uModelFactory_, 1, GL_FALSE, model.constData());

    const QVector3D eye = (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();
    gl_->glUniform3f(uViewPosFactory_, eye.x(), eye.y(), eye.z());
    gl_->glUniform3f(uLightDirFactory_, 0.35f, 1.0f, 0.2f);
    gl_->glUniform3f(uColorFactory_, 1.0f, 1.0f, 1.0f);
    gl_->glUniform1i(uUseTextureFactory_, 1);

    mineModel_->draw(progFactory_, mvpMine, model, ctx.camera.view);

    if (blendWasEnabled) gl_->glEnable(GL_BLEND);
    else gl_->glDisable(GL_BLEND);

    if (cullWasEnabled) gl_->glEnable(GL_CULL_FACE);
    else gl_->glDisable(GL_CULL_FACE);

    if (depthTestWasEnabled) gl_->glEnable(GL_DEPTH_TEST);
    else gl_->glDisable(GL_DEPTH_TEST);

    gl_->glDepthMask(depthWriteMask);
}

void EntityRenderer::renderTrees(const HexSphereRenderer::RenderContext& ctx) const {
    if ((!treeModel_ || !treeModel_->isInitialized()) &&
        (!firTreeModel_ || !firTreeModel_->isInitialized())) return;

    if (progModel_ == 0) return;

    gl_->glUseProgram(progModel_);

    const GLint uIsCar = gl_->glGetUniformLocation(progModel_, "uIsCar");
    const GLint uUseFoliageColor = gl_->glGetUniformLocation(progModel_, "uUseFoliageColor");
    const GLint uFoliageColor = gl_->glGetUniformLocation(progModel_, "uFoliageColor");
    const GLint uTrunkColor = gl_->glGetUniformLocation(progModel_, "uTrunkColor");
    const GLint uWindTime = gl_->glGetUniformLocation(progModel_, "uWindTime");

    if (uIsCar >= 0) gl_->glUniform1i(uIsCar, 0);
    static float foliageWindTime = 0.0f;
    foliageWindTime += 0.016f;
    if (uWindTime >= 0) {
        gl_->glUniform1f(uWindTime, foliageWindTime);
    }

    const GLboolean cullWasEnabled = gl_->glIsEnabled(GL_CULL_FACE);
    gl_->glDisable(GL_CULL_FACE);

    const QVector3D globalLightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
    const QVector3D eye = (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

    gl_->glUniform3f(uLightDir_, globalLightDir.x(), globalLightDir.y(), globalLightDir.z());
    gl_->glUniform3f(uViewPos_, eye.x(), eye.y(), eye.z());

    const auto& placements = ctx.graph.scene.getTreePlacements();

    constexpr size_t kMaxRenderedTrees = 96;
    size_t renderedTrees = 0;
    for (const auto& placement : placements) {
        if (renderedTrees >= kMaxRenderedTrees) {
            break;
        }

        const QVector3D treePos = computeSurfacePoint(ctx.graph.scene, placement, ctx.graph.heightStep);

        QMatrix4x4 model;
        model.translate(treePos);
        orientToSurfaceNormal(model, treePos.normalized());
        model.rotate(placement.rotation * 180.0f / 3.14159f, 0, 1, 0);

        const float baseScale = (placement.treeType == TreeType::Fir) ? 0.045f : 0.04f;
        model.scale(baseScale * placement.scale);

        const QMatrix4x4 mvpTree = ctx.camera.projection * ctx.camera.view * model;
        const auto& currentModel = (placement.treeType == TreeType::Fir)
            ? firTreeModel_
            : treeModel_;

        if (!currentModel || !currentModel->isInitialized()) continue;

        const bool hasTrunk = currentModel->hasDrawablePart("trunk");
        const bool hasFoliage = currentModel->hasDrawablePart("foliage");

        if (hasTrunk) {
            if (uUseFoliageColor >= 0) {
                gl_->glUniform1i(uUseFoliageColor, 0);
            }
            if (uTrunkColor >= 0) {
                gl_->glUniform3f(uTrunkColor,
                    placement.trunkColor.x(),
                    placement.trunkColor.y(),
                    placement.trunkColor.z());
            }
            currentModel->drawPart("trunk", progModel_, mvpTree, model, ctx.camera.view);
        }

        if (hasFoliage) {
            if (uUseFoliageColor >= 0) {
                gl_->glUniform1i(uUseFoliageColor, 1);
            }
            if (uFoliageColor >= 0) {
                gl_->glUniform3f(uFoliageColor,
                    placement.foliageColor.x(),
                    placement.foliageColor.y(),
                    placement.foliageColor.z());
            }
            currentModel->drawPart("foliage", progModel_, mvpTree, model, ctx.camera.view);
        }

        if (!hasTrunk && !hasFoliage) {
            if (uUseFoliageColor >= 0) {
                gl_->glUniform1i(uUseFoliageColor, 1);
            }
            if (uFoliageColor >= 0) {
                gl_->glUniform3f(uFoliageColor,
                    placement.foliageColor.x(),
                    placement.foliageColor.y(),
                    placement.foliageColor.z());
            }
            currentModel->draw(progModel_, mvpTree, model, ctx.camera.view, placement.foliageColor, false);
        }

        ++renderedTrees;
    }

    if (cullWasEnabled) gl_->glEnable(GL_CULL_FACE);
    else gl_->glDisable(GL_CULL_FACE);
}
