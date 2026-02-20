#include "controllers/InputController.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QWheelEvent>
#include <QtDebug>
#include <algorithm>
#include <cmath>
#include <limits>

#include "controllers/CameraController.h"
#include "ECS/Transform.h"
#include "model/SurfacePlacement.h"

namespace {
bool rayTriangleMT(const QVector3D& o, const QVector3D& d,
    const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
    float& tOut) {
    const float EPS = 1e-6f;
    const QVector3D e1 = v1 - v0;
    const QVector3D e2 = v2 - v0;
    const QVector3D p = QVector3D::crossProduct(d, e2);
    const float det = QVector3D::dotProduct(e1, p);
    if (std::fabs(det) < EPS) return false;
    const float invDet = 1.0f / det;
    const QVector3D t = o - v0;
    const float u = QVector3D::dotProduct(t, p) * invDet; if (u < -EPS || u > 1.0f + EPS) return false;
    const QVector3D q = QVector3D::crossProduct(t, e1);
    const float v = QVector3D::dotProduct(d, q) * invDet; if (v < -EPS || u + v > 1.0f + EPS) return false;
    const float tt = QVector3D::dotProduct(e2, q) * invDet; if (tt <= EPS) return false;
    tOut = tt; return true;
}

static void printGlInfo(QOpenGLFunctions_3_3_Core* gl) {
    const GLubyte* vendor = gl->glGetString(GL_VENDOR);
    const GLubyte* renderer = gl->glGetString(GL_RENDERER);
    const GLubyte* version = gl->glGetString(GL_VERSION);

    qDebug() << "=== OpenGL Device Info ===";
    qDebug() << "GPU Vendor:   " << reinterpret_cast<const char*>(vendor);
    qDebug() << "GPU Renderer: " << reinterpret_cast<const char*>(renderer);
    qDebug() << "GL Version:   " << reinterpret_cast<const char*>(version);
    qDebug() << "===========================";
}
}

InputController::InputController(CameraController& camera)
    : camera_(camera) {}

void InputController::initialize(QOpenGLWidget* owner) {
    owner_ = owner;
    owner_->makeCurrent();

    QOpenGLContext* ctx = owner_->context();
    if (!ctx) qFatal("No OpenGL context!");

    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(ctx);
    if (!gl) {
        qFatal("Cannot obtain QOpenGLFunctions_3_3_Core");
    }

    gl->initializeOpenGLFunctions();
    printGlInfo(gl);

    renderer_ = std::make_unique<HexSphereRenderer>(owner_);
    renderer_->initialize(owner_, gl, &stats_);
    if (renderer_)
		qDebug() << "[InputController::initialize] renderer initialized successfully";
    else
        qDebug() << "[InputController::initialize] renderer ready=" << renderer_->ready()
        << "ownerContext=" << owner_->context();


    auto& pyramid = ecs_.createEntity("Explorer");
    pyramid.currentCell = 0;
    ecs_.emplace<ecs::Mesh>(pyramid.id).meshId = "pyramid";
    ecs::Transform& transform = ecs_.emplace<ecs::Transform>(pyramid.id);
    QVector3D surfacePosition = computeSurfacePoint(scene_, 0);
    transform.position = ecs::localToWorldPoint(transform, ecs::CoordinateFrame{}, surfacePosition);
    ecs_.emplace<ecs::Collider>(pyramid.id).radius = 0.08f;

    Response initResponse;
    rebuildModel(initResponse);
}

void InputController::resize(int w, int h, float devicePixelRatio) {
    camera_.resize(w, h, devicePixelRatio);
}

void InputController::beginFrameContext() {
    if (renderer_) {
        renderer_->beginExternalContext();
    }
}

void InputController::endFrameContext() {
    if (renderer_) {
        renderer_->endExternalContext();
    }
}

InputController::Response InputController::render() {
    Response response;
    if (!renderer_) return response;

    HexSphereRenderer::RenderGraph graph{scene_, ecs_, scene_.heightStep()};
    HexSphereRenderer::RenderCamera camera{camera_.view(), camera_.projection()};
    HexSphereRenderer::SceneLighting lighting{lightDir_, waterTime_};
    renderer_->renderScene(graph, camera, lighting);
    stats_.frameRendered();
    return response;
}

InputController::Response InputController::mousePress(QMouseEvent* e) {
    Response response;
    lastPos_ = e->pos();

    if (e->button() == Qt::RightButton) {
        rotating_ = true;
        return response;
    }

    if (e->button() == Qt::LeftButton) {
        auto p = e->position();
        auto hit = pickSceneAt(p.x(), p.y());
        if (!hit) return response;
        if (hit->isEntity) {
            selectEntity(hit->entityId, response);
        }
        else if (hit->cellId >= 0) {
            response.toggleCellId = hit->cellId;
            // Current UX: the same click both toggles selection (via command path)
            // and moves currently selected entity, if any.
            moveSelectedEntityToCell(hit->cellId, response);
        }
        response.requestUpdate = true;
    }
    return response;
}

InputController::Response InputController::mouseMove(QMouseEvent* e) {
    Response response;
    if (!rotating_) return response;

    const QPoint currentPos = e->pos();
    const QPoint delta = currentPos - lastPos_;
    lastPos_ = currentPos;

    if (delta.manhattanLength() == 0) return response;

    camera_.rotate(delta);
    response.requestUpdate = true;
    return response;
}

void InputController::mouseRelease(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        rotating_ = false;
    }
}

InputController::Response InputController::wheel(QWheelEvent* e) {
    Response response;
    const float steps = (e->angleDelta().y() / 8.0f) / 15.0f;
    camera_.zoom(steps);
    response.requestUpdate = true;
    return response;
}

InputController::Response InputController::keyPress(QKeyEvent* e) {
    Response response;
    switch (e->key()) {
    case Qt::Key_C:
        clearPath(response);
        return response;
    case Qt::Key_O:
        return toggleOreVisualization();
    case Qt::Key_S:
        scene_.setSmoothOneStep(!scene_.smoothOneStep());
        rebuildModel(response);
        response.hudMessage = QString("Smooth mode: ") + (scene_.smoothOneStep() ? "ON" : "OFF");
        return response;
    case Qt::Key_P:
        buildAndShowSelectedPath(response);
        return response;
    case Qt::Key_W:
    {
        auto sel = ecs_.selectedEntity();
        if (!sel) return response;
        ecs::Entity& ent = sel->get();
        const auto& cells = scene_.model().cells();
        if (ent.currentCell < 0 || ent.currentCell >= (int)cells.size()) return response;
        const auto& c = cells[(size_t)ent.currentCell];
        if (c.neighbors.empty()) return response;
        int next = c.neighbors[0];
        if (next < 0) return response;
        ent.currentCell = next;
        if (auto* transform = ecs_.get<ecs::Transform>(ent.id)) {
            transform->position = computeSurfacePoint(scene_, next);
        }
        response.requestUpdate = true;
        break;
    }
    case Qt::Key_Escape:
        deselectEntity();
        response.requestUpdate = true;
        return response;
    case Qt::Key_Delete:
        if (selectedEntityId_ != -1) {
            ecs_.destroyEntity(selectedEntityId_);
            selectedEntityId_ = -1;
            response.requestUpdate = true;
        }
        return response;
    default: break;
    }

    if (scene_.selectedCells().empty()) return response;

    auto apply = [&](auto fn) { for (int cid : scene_.selectedCells()) fn(cid); };
    switch (e->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        apply([&](int cid) { scene_.modelMutable().addHeight(cid, +1); });
        break;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        apply([&](int cid) { scene_.modelMutable().addHeight(cid, -1); });
        break;
    case Qt::Key_1:
        apply([&](int cid) { scene_.modelMutable().setBiome(cid, Biome::Sea); });
        break;
    case Qt::Key_2:
        apply([&](int cid) { scene_.modelMutable().setBiome(cid, Biome::Grass); });
        break;
    case Qt::Key_3:
        apply([&](int cid) { scene_.modelMutable().setBiome(cid, Biome::Rock); });
        break;
    case Qt::Key_4:
        apply([&](int cid) { scene_.modelMutable().setBiome(cid, Biome::Snow); });
        break;
    case Qt::Key_5:
        apply([&](int cid) { scene_.modelMutable().setBiome(cid, Biome::Tundra); });
        break;
    case Qt::Key_6:
        apply([&](int cid) { scene_.modelMutable().setBiome(cid, Biome::Desert); });
        break;
    case Qt::Key_7:
        apply([&](int cid) { scene_.modelMutable().setBiome(cid, Biome::Savanna); });
        break;
    case Qt::Key_8:
        apply([&](int cid) { scene_.modelMutable().setBiome(cid, Biome::Jungle); });
        break;
    default: return response;
    }
    rebuildModel(response);
    return response;
}

InputController::Response InputController::setSubdivisionLevel(int L) {
    Response response;
    if (scene_.subdivisionLevel() != L) {
        scene_.setSubdivisionLevel(L);
        stats_.setSubdivisionLevel(L);
        updateBufferUsageStrategy();
        rebuildModel(response);
        response.requestUpdate = true;
    }
    return response;
}

InputController::Response InputController::resetView() {
    Response response;
    camera_.reset();
    response.requestUpdate = true;
    return response;
}

InputController::Response InputController::clearSelection() {
    Response response;
    scene_.clearSelection();
    uploadSelection();
    response.requestUpdate = true;
    return response;
}

InputController::Response InputController::setTerrainParams(const TerrainParams& p) {
    Response response;
    scene_.setGenParams(p);
    return response;
}

InputController::Response InputController::setGeneratorByIndex(int idx) {
    Response response;
    scene_.setGeneratorByIndex(idx);
    return response;
}

InputController::Response InputController::regenerateTerrain() {
    Response response;
    scene_.regenerateTerrain();
    rebuildModel(response);
    return response;
}

InputController::Response InputController::setSmoothOneStep(bool on) {
    Response response;
    scene_.setSmoothOneStep(on);
    rebuildModel(response);
    return response;
}

InputController::Response InputController::setStripInset(float v) {
    Response response;
    scene_.setStripInset(v);
    rebuildModel(response);
    return response;
}

InputController::Response InputController::setOutlineBias(float v) {
    Response response;
    scene_.setOutlineBias(v);
    rebuildModel(response);
    return response;
}

InputController::Response InputController::toggleCellSelection(int cellId) {
    Response response;
    scene_.toggleCellSelection(cellId);

    // Lightweight path: updates only selection-outline buffer (no full uploadScene).
    uploadSelection();

    response.requestUpdate = true;
    return response;
}

InputController::Response InputController::advanceWaterTime(float dt) {
    Response response;
    waterTime_ += dt;
    response.requestUpdate = true;
    return response;
}

void InputController::rebuildModel(Response& response) {
    scene_.rebuildModel();
    uploadBuffers();
    response.requestUpdate = true;
}

void InputController::uploadSelection() {
    if (renderer_) {
        renderer_->uploadSelectionOutline(scene_.buildSelectionOutlineVertices());
    }
}

void InputController::uploadBuffers() {
    if (renderer_) {
        if (!renderer_->ready()) {
            qCritical() << "[InputController::uploadBuffers] renderer is not ready, uploads will be skipped";
        }
        renderer_->uploadScene(scene_, uploadOptions_);
    }
}

void InputController::buildAndShowSelectedPath(Response& response) {
    if (renderer_) {
        if (auto poly = scene_.buildPathPolyline()) {
            renderer_->uploadPath(*poly);
        } else {
            renderer_->uploadPath({});
        }
    }
    response.requestUpdate = true;
}

void InputController::clearPath(Response& response) {
    if (renderer_) {
        renderer_->uploadPath({});
    }
    response.requestUpdate = true;
}

void InputController::updateBufferUsageStrategy() {
    const int L = scene_.subdivisionLevel();
    if (L >= 4) {
        uploadOptions_.terrainUsage = GL_DYNAMIC_DRAW;
        uploadOptions_.wireUsage = GL_DYNAMIC_DRAW;
        uploadOptions_.useStaticBuffers = false;
    }
    else {
        uploadOptions_.terrainUsage = GL_STATIC_DRAW;
        uploadOptions_.wireUsage = GL_STATIC_DRAW;
        uploadOptions_.useStaticBuffers = true;
    }
    qDebug() << "Buffer strategy:" << (uploadOptions_.useStaticBuffers ? "STATIC" : "DYNAMIC") << "for L =" << L;
}

std::optional<int> InputController::pickCellAt(int sx, int sy) const {
    const QVector3D ro = camera_.rayOrigin();
    const QVector3D rd = camera_.rayDirectionFromScreen(sx, sy, owner_->width(), owner_->height(), owner_->devicePixelRatioF());
    const auto& tris = scene_.model().pickTris();
    float bestT = std::numeric_limits<float>::infinity();
    int bestId = -1;
    for (const auto& pt : tris) {
        float t;
        if (rayTriangleMT(ro, rd, pt.v0, pt.v1, pt.v2, t))
            if (t < bestT) { bestT = t; bestId = pt.cellId; }
    }
    if (bestId >= 0) return bestId;
    else return std::nullopt;
}

std::optional<InputController::PickHit> InputController::pickTerrainAt(int sx, int sy) const {
    if (scene_.terrain().triOwner.empty()) return std::nullopt;
    const QVector3D ro = camera_.rayOrigin();
    const QVector3D rd = camera_.rayDirectionFromScreen(sx, sy, owner_->width(), owner_->height(), owner_->devicePixelRatioF());

    float bestT = std::numeric_limits<float>::infinity();
    int   bestOwner = -1;
    QVector3D bestPos;

    const auto& P = scene_.terrain().pos;
    const auto& I = scene_.terrain().idx;
    const auto& O = scene_.terrain().triOwner;

    const size_t triCount = O.size();
    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t i0 = I[3 * t + 0], i1 = I[3 * t + 1], i2 = I[3 * t + 2];
        const QVector3D v0(P[3 * i0], P[3 * i0 + 1], P[3 * i0 + 2]);
        const QVector3D v1(P[3 * i1], P[3 * i1 + 1], P[3 * i1 + 2]);
        const QVector3D v2(P[3 * i2], P[3 * i2 + 1], P[3 * i2 + 2]);
        float tt;
        if (rayTriangleMT(ro, rd, v0, v1, v2, tt) && tt < bestT) {
            bestT = tt; bestOwner = O[t]; bestPos = ro + rd * tt;
        }
    }
    if (bestOwner >= 0) {
        return PickHit{ bestOwner, -1, bestPos, bestT, false };
    }
    return std::nullopt;
}

std::optional<InputController::PickHit> InputController::pickEntityAt(int sx, int sy) const {
    const QVector3D ro = camera_.rayOrigin();
    const QVector3D rd = camera_.rayDirectionFromScreen(sx, sy, owner_->width(), owner_->height(), owner_->devicePixelRatioF());

    float bestT = std::numeric_limits<float>::infinity();
    int bestEntityId = -1;
    QVector3D bestPos;

    ecs_.each<ecs::Collider, ecs::Transform>([&](const ecs::Entity& e, const ecs::Collider& collider, const ecs::Transform& transform) {
        const QVector3D center = transform.position;
        const float radius = collider.radius;
        const QVector3D oc = ro - center;
        const float b = 2.0f * QVector3D::dotProduct(oc, rd);
        const float c = QVector3D::dotProduct(oc, oc) - radius * radius;
        const float discriminant = b * b - 4.0f * c;
        if (discriminant < 0) return;
        const float sqrtDisc = std::sqrt(discriminant);
        const float t0 = (-b - sqrtDisc) * 0.5f;
        const float t1 = (-b + sqrtDisc) * 0.5f;
        float t = (t0 > 0) ? t0 : t1;
        if (t > 0 && t < bestT) {
            bestT = t;
            bestEntityId = e.id;
            bestPos = ro + rd * t;
        }
    });

    if (bestEntityId != -1) {
        return PickHit{ -1, bestEntityId, bestPos, bestT, true };
    }
    return std::nullopt;
}

std::optional<InputController::PickHit> InputController::pickSceneAt(int sx, int sy) const {
    auto entityHit = pickEntityAt(sx, sy);
    auto terrainHit = pickTerrainAt(sx, sy);

    if (entityHit && terrainHit) {
        return (entityHit->t < terrainHit->t) ? entityHit : terrainHit;
    }
    return entityHit ? entityHit : terrainHit;
}

void InputController::selectEntity(int entityId, Response& response) {
    if (selectedEntityId_ == entityId) {
        deselectEntity();
        response.requestUpdate = true;
        return;
    }

    if (auto* previous = ecs_.getEntity(selectedEntityId_)) {
        previous->selected = false;
    }

    selectedEntityId_ = entityId;
    ecs_.setSelected(entityId, true);
    response.requestUpdate = true;
}

void InputController::deselectEntity() {
    if (selectedEntityId_ != -1) {
        ecs_.setSelected(selectedEntityId_, false);
        selectedEntityId_ = -1;
    }
}

void InputController::moveSelectedEntityToCell(int cellId, Response& response) {
    if (selectedEntityId_ == -1) return;
    auto* entity = ecs_.getEntity(selectedEntityId_);
    if (!entity) return;
    if (cellId >= 0 && cellId < scene_.model().cellCount()) {
        entity->currentCell = cellId;
        if (auto* transform = ecs_.get<ecs::Transform>(entity->id)) {
            transform->position = computeSurfacePoint(scene_, cellId);
        }
    }
    deselectEntity();
    response.requestUpdate = true;
}


InputController::Response InputController::toggleOreVisualization() {
    oreVisualizationEnabled_ = !oreVisualizationEnabled_;
    oreAnimationTime_ = 0.0f;

    qDebug() << "Ore visualization toggled to:" << oreVisualizationEnabled_;

    Response r;
    r.requestUpdate = true;
    r.hudMessage = oreVisualizationEnabled_
        ? QString("Ore visualization: ON")
        : QString("Ore visualization: OFF");
    return r;
}

void InputController::setOreAnimationTime(float time) {
    oreAnimationTime_ = time;
}

void InputController::setOreVisualizationEnabled(bool enabled) {
    oreVisualizationEnabled_ = enabled;
}

float InputController::getOreAnimationTime() const {
    return oreAnimationTime_;
}

bool InputController::isOreVisualizationEnabled() const {
    return oreVisualizationEnabled_;
}

HexSphereModel* InputController::getModel() {
    // Ïîëó÷àåì ìîäåëü èç ñöåíû
    return &scene_.modelMutable();
}

InputController::Response InputController::setOreAnimationSpeed(float speed) {
    oreAnimationSpeed_ = std::clamp(speed, 0.0f, 2.0f);

    Response r;
    r.requestUpdate = true;
    r.hudMessage = QString("Ore animation speed: %1").arg(oreAnimationSpeed_);
    return r;
}

InputController::Response InputController::regenerateOreDeposits() {
    Response r;
    r.requestUpdate = true;
    r.hudMessage = QString("Ore deposits regenerated");
    return r;
}

