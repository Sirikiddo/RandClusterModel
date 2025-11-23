#include "EntityRenderer.h"

#include "SurfacePlacement.h"

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

} // namespace

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
                               const std::shared_ptr<ModelHandler>& treeModel)
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
    , treeModel_(treeModel) {}

void EntityRenderer::renderEntities(const HexSphereRenderer::RenderContext& ctx) const {
    const QMatrix4x4& view = ctx.camera.view;
    const QMatrix4x4& proj = ctx.camera.projection;
    const QMatrix4x4 vp = proj * view;
    ctx.graph.ecs.each<ecs::Mesh, ecs::Transform>([&](const ecs::Entity& e, const ecs::Mesh&, const ecs::Transform& transform) {
        QVector3D surfacePos = (e.currentCell >= 0)
            ? computeSurfacePoint(ctx.graph.scene, e.currentCell, ctx.graph.heightStep)
            : transform.position;

        QMatrix4x4 model;
        model.translate(surfacePos);
        QVector3D surfaceNormal = surfacePos.normalized();
        orientToSurfaceNormal(model, surfaceNormal);
        model.scale(0.08f);
        if (e.selected) {
            model.scale(1.2f);
        }

        const QMatrix4x4 entityMvp = vp * model;
        if (e.selected) {
            gl_->glUseProgram(progSel_);
            gl_->glUniformMatrix4fv(uMvpSel_, 1, GL_FALSE, entityMvp.constData());
        } else {
            gl_->glUseProgram(progWire_);
            gl_->glUniformMatrix4fv(uMvpWire_, 1, GL_FALSE, entityMvp.constData());
        }
        gl_->glBindVertexArray(vaoPyramid_);
        gl_->glDrawArrays(GL_TRIANGLES, 0, pyramidVertexCount_);
    });
}

void EntityRenderer::renderTrees(const HexSphereRenderer::RenderContext& ctx) const {
    if (!treeModel_ || !treeModel_->isInitialized() || progModel_ == 0 || treeModel_->isEmpty()) return;

    gl_->glUseProgram(progModel_);

    QVector3D globalLightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
    QVector3D eye = (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

    gl_->glUniform3f(uLightDir_, globalLightDir.x(), globalLightDir.y(), globalLightDir.z());
    gl_->glUniform3f(uViewPos_, eye.x(), eye.y(), eye.z());
    gl_->glUniform3f(uColor_, 0.15f, 0.5f, 0.1f);
    gl_->glUniform1i(uUseTexture_, treeModel_->hasUVs() ? 1 : 0);

    const auto& cells = ctx.graph.scene.model().cells();
    int treesRendered = 0;
    const int maxTrees = 25;

    for (size_t i = 0; i < cells.size() && treesRendered < maxTrees; ++i) {
        if (cells[i].biome == Biome::Grass && (i % 3 == 0)) {
            QVector3D treePos = computeSurfacePoint(ctx.graph.scene, static_cast<int>(i), ctx.graph.heightStep);

            QMatrix4x4 model;
            model.translate(treePos);
            orientToSurfaceNormal(model, treePos.normalized());
            model.scale(0.05f + 0.02f * (i % 5));

            QMatrix4x4 mvpTree = ctx.camera.projection * ctx.camera.view * model;
            gl_->glUniformMatrix4fv(uMvpModel_, 1, GL_FALSE, mvpTree.constData());
            gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());

            treeModel_->draw(progModel_, mvpTree, model, ctx.camera.view);
            ++treesRendered;
        }
    }
}

