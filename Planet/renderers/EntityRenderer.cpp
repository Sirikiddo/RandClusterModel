#include "renderers/EntityRenderer.h"
#include "model/SurfacePlacement.h"
#include <random>
#include <cmath>

namespace {
    void orientToSurfaceNormal(QMatrix4x4& matrix, const QVector3D& normal) {
        QVector3D up = normal.normalized();

        QVector3D forward = (qAbs(QVector3D::dotProduct(up, QVector3D(0, 0, 1))) > 0.99f)
            ? QVector3D(1, 0, 0)
            : QVector3D(0, 0, 1);

        QVector3D right = QVector3D::crossProduct(forward, up).normalized();
        forward = QVector3D::crossProduct(up, right).normalized();

        QMatrix4x4 rotation;
        rotation.setColumn(0, QVector4D(right, 0.0f));
        rotation.setColumn(1, QVector4D(up, 0.0f));
        rotation.setColumn(2, QVector4D(forward, 0.0f));
        rotation.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));

        matrix = matrix * rotation;
    }
}

EntityRenderer::EntityRenderer(QOpenGLFunctions_3_3_Core* gl,
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
    const std::shared_ptr<CarModelHandler>& carModel)
    : gl_(gl)
    , progWire_(progWire)
    , progSel_(progSel)
    , progModel_(progModel)
    , uMvpWire_(uMvpWire)
    , uMvpSel_(uMvpSel)
    , uMvpModel_(uMvpModel)
    , uModel_(uModel)
    , uLightDir_(uLightDir)
    , uViewPos_(uViewPos)
    , uColor_(uColor)
    , uUseTexture_(uUseTexture)
    , vaoPyramid_(vaoPyramid)
    , pyramidVertexCount_(pyramidVertexCount)
    , treeModel_(treeModel)
    , carModel_(carModel) {
}

void EntityRenderer::renderEntities(const HexSphereRenderer::RenderContext& ctx) const {
    const QMatrix4x4& view = ctx.camera.view;
    const QMatrix4x4& proj = ctx.camera.projection;
    const QMatrix4x4 vp = proj * view;

    ctx.graph.ecs.each<ecs::Mesh, ecs::Transform>([&](const ecs::Entity& e, const ecs::Mesh&, const ecs::Transform& transform) {
        renderCar(ctx, e);
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

    const float heightOffset = 0.005f;
    QVector3D elevatedPos = surfacePos.normalized() * (surfacePos.length() + heightOffset);

    const GLboolean depthTestWasEnabled = gl_->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasEnabled = gl_->glIsEnabled(GL_BLEND);
    const GLboolean cullWasEnabled = gl_->glIsEnabled(GL_CULL_FACE);
    GLboolean depthWriteMask = GL_TRUE;
    gl_->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);

    // Keep the car opaque and double-sided: the imported OBJ has mixed winding on some parts.
    gl_->glDisable(GL_BLEND);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glDepthMask(GL_TRUE);
    gl_->glDisable(GL_CULL_FACE);

    QMatrix4x4 model;
    model.translate(elevatedPos);

    const QVector3D up = elevatedPos.normalized();

    auto basisFromHorizontalForward = [](const QVector3D& unitUp, QVector3D forwardTangent) {
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
        QMatrix4x4 r;
        r.setColumn(0, QVector4D(right, 0.0f));
        r.setColumn(1, QVector4D(unitUp, 0.0f));
        r.setColumn(2, QVector4D(forwardTangent, 0.0f));
        return r;
    };

    QMatrix4x4 orientation;
    auto* anim = ctx.graph.ecs.get<ecs::Animation>(entity.id);
    auto* transform = ctx.graph.ecs.get<ecs::Transform>(entity.id);

    QVector3D forwardTangent(0, 0, 0);
    if (anim && anim->type == ecs::Animation::Type::MoveTo && anim->surfaceForward.length() > 0.5f) {
        forwardTangent = anim->surfaceForward;
    }
    else if (transform && transform->surfaceForward.length() > 0.5f) {
        forwardTangent = transform->surfaceForward;
    }

    orientation = basisFromHorizontalForward(up, forwardTangent);

    model = model * orientation;

    const float carScale = 2.5f;
    model.scale(carScale);

    if (entity.selected) {
        model.scale(1.2f);
    }

    const QMatrix4x4 mvpCar = ctx.mvp * model;

    gl_->glUseProgram(progModel_);

    // ?????: glUniform* ?????? uniform ?????? ??? ??????? ???????? ?????????,
    // ??????? ?????????? uIsCar ????? glUseProgram.
    const GLint uIsCar = gl_->glGetUniformLocation(progModel_, "uIsCar");
    if (uIsCar >= 0) gl_->glUniform1i(uIsCar, 1);

    gl_->glUniformMatrix4fv(uMvpModel_, 1, GL_FALSE, mvpCar.constData());
    gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());

    QVector3D eye = (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();
    gl_->glUniform3f(uViewPos_, eye.x(), eye.y(), eye.z());
    gl_->glUniform3f(uLightDir_, 0.5f, 1.0f, 0.3f);
    // ?????? ?????? ??? ?????? (?????? ???? ?????? shader tint-??).
    // ??? ?????? uColor ???????????? ?????? ??? fallback (???? ? submesh ??? ????????).
    // uColor ??? ?????? ????? ????????????? per-submesh (Kd ?? MTL) ? CarModelHandler.
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

void EntityRenderer::renderTrees(const HexSphereRenderer::RenderContext& ctx) const {
    if (!treeModel_ || !treeModel_->isInitialized() || progModel_ == 0 || treeModel_->isEmpty()) return;

    gl_->glUseProgram(progModel_);

    // ???????: ???-????? ??????? ?? ??????????.
    const GLint uIsCar = gl_->glGetUniformLocation(progModel_, "uIsCar");
    if (uIsCar >= 0) gl_->glUniform1i(uIsCar, 0);

    QVector3D globalLightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
    QVector3D eye = (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

    gl_->glUniform3f(uLightDir_, globalLightDir.x(), globalLightDir.y(), globalLightDir.z());
    gl_->glUniform3f(uViewPos_, eye.x(), eye.y(), eye.z());
    gl_->glUniform3f(uColor_, 0.15f, 0.5f, 0.1f);
    gl_->glUniform1i(uUseTexture_, treeModel_->hasUVs() ? 1 : 0);

    const auto& placements = ctx.graph.scene.getTreePlacements();

    for (const auto& placement : placements) {
        QVector3D treePos = computeSurfacePoint(ctx.graph.scene, placement, ctx.graph.heightStep);

        QMatrix4x4 model;
        model.translate(treePos);
        orientToSurfaceNormal(model, treePos.normalized());

        float scale = 0.05f + 0.02f * (placement.cellId % 5);
        model.scale(scale);

        QMatrix4x4 mvpTree = ctx.camera.projection * ctx.camera.view * model;
        gl_->glUniformMatrix4fv(uMvpModel_, 1, GL_FALSE, mvpTree.constData());
        gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());

        treeModel_->draw(progModel_, mvpTree, model, ctx.camera.view);
    }
}
