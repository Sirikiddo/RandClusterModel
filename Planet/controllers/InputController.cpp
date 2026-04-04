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
#include "controllers/PathBuilder.h"
#include "ECS/Transform.h"
#include "model/SurfacePlacement.h"

namespace {

    constexpr int kPathSegmentsPerEdge = 8;
    constexpr float kEntitySurfaceOffset = 0.0f;
    constexpr float kBaseTraversalSpeed = 0.35f;

    bool rayTriangleMT(const QVector3D& o, const QVector3D& d,
        const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
        float& tOut) {
        const float eps = 1e-6f;
        const QVector3D e1 = v1 - v0;
        const QVector3D e2 = v2 - v0;
        const QVector3D p = QVector3D::crossProduct(d, e2);
        const float det = QVector3D::dotProduct(e1, p);
        if (std::fabs(det) < eps) return false;

        const float invDet = 1.0f / det;
        const QVector3D t = o - v0;
        const float u = QVector3D::dotProduct(t, p) * invDet;
        if (u < -eps || u > 1.0f + eps) return false;

        const QVector3D q = QVector3D::crossProduct(t, e1);
        const float v = QVector3D::dotProduct(d, q) * invDet;
        if (v < -eps || u + v > 1.0f + eps) return false;

        const float tt = QVector3D::dotProduct(e2, q) * invDet;
        if (tt <= eps) return false;

        tOut = tt;
        return true;
    }

    void printGlInfo(QOpenGLFunctions_3_3_Core* gl) {
        const GLubyte* vendor = gl->glGetString(GL_VENDOR);
        const GLubyte* renderer = gl->glGetString(GL_RENDERER);
        const GLubyte* version = gl->glGetString(GL_VERSION);

        qDebug() << "=== OpenGL Device Info ===";
        qDebug() << "GPU Vendor:   " << reinterpret_cast<const char*>(vendor);
        qDebug() << "GPU Renderer: " << reinterpret_cast<const char*>(renderer);
        qDebug() << "GL Version:   " << reinterpret_cast<const char*>(version);
        qDebug() << "===========================";
    }

    float landingDurationMultiplier(Biome biome) {
        switch (biome) {
        case Biome::Sea:  return 1.25f;
        case Biome::Snow: return 1.10f;
        default:          return 1.0f;
        }
    }

    float bounceHeightForBiome(Biome biome) {
        switch (biome) {
        case Biome::Sea:  return 0.018f;
        case Biome::Snow: return 0.028f;
        case Biome::Rock: return 0.030f;
        default:          return 0.035f;
        }
    }

    float arcPeakForBiome(Biome biome) {
        return biome == Biome::Sea ? 0.32f : 0.5f;
    }

    bool usesSoftLanding(Biome biome) {
        return biome == Biome::Sea;
    }

    int pathSmoothDelta(const HexSphereSceneController& scene) {
        return scene.smoothOneStep() ? 1 : 0;
    }

    int pathClimbLimit(const HexSphereSceneController& scene) {
        return PathBuilder::effectiveMaxClimbDelta(pathSmoothDelta(scene));
    }

} // namespace

InputController::InputController(CameraController& camera)
    : camera_(camera) {
}

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

    auto& pyramid = ecs_.createEntity("Explorer");
    pyramid.currentCell = 0;
    ecs_.emplace<ecs::Mesh>(pyramid.id).meshId = "pyramid";
    ecs::Transform& transform = ecs_.emplace<ecs::Transform>(pyramid.id);
    const QVector3D surfacePosition = computeSurfacePoint(scene_, 0, scene_.heightStep(), kEntitySurfaceOffset);
    transform.position = ecs::localToWorldPoint(transform, ecs::CoordinateFrame{}, surfacePosition);
    ecs_.emplace<ecs::Collider>(pyramid.id).radius = 0.08f;

    Response initResponse;
    rebuildModel(initResponse);
}

void InputController::resize(int w, int h, float devicePixelRatio) {
    camera_.resize(w, h, devicePixelRatio);
}

InputController::Response InputController::render() {
    Response response;
    if (!renderer_) return response;

    HexSphereRenderer::RenderGraph graph{ scene_, ecs_, scene_.heightStep() };
    HexSphereRenderer::RenderCamera camera{ camera_.view(), camera_.projection() };
    HexSphereRenderer::SceneLighting lighting{ lightDir_, waterTime_ };
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
        const auto p = e->position();
        auto hit = pickSceneAt(p.x(), p.y());
        if (!hit) return response;

        if (hit->isEntity) {
            selectEntity(hit->entityId, response);
        }
        else if (hit->cellId >= 0) {
            scene_.toggleCellSelection(hit->cellId);
            uploadSelection();
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
        auto selected = ecs_.selectedEntity();
        if (!selected) return response;

        ecs::Entity& entity = selected->get();
        if (ecs_.get<ecs::Animation>(entity.id)) {
            response.hudMessage = QString("Explorer is already moving");
            response.requestUpdate = true;
            return response;
        }

        const auto& cells = scene_.model().cells();
        if (entity.currentCell < 0 || entity.currentCell >= static_cast<int>(cells.size())) return response;
        const auto& currentCell = cells[static_cast<size_t>(entity.currentCell)];
        if (currentCell.neighbors.empty()) return response;

        bool moved = false;
        for (int next : currentCell.neighbors) {
            if (next < 0) continue;
            buildAndShowPathBetween(entity.currentCell, next, response);
            // Для Explorer-движения хотим прямую "по поверхности", без подпрыгивания
            moved = applyAnimation(entity.id, next, kBaseTraversalSpeed, 0.0f);
            if (moved) {
                break;
            }
        }

        if (!moved) {
            if (renderer_) {
                renderer_->uploadPath({});
            }
            response.hudMessage = QString("No accessible neighboring cell");
        }
        response.requestUpdate = true;
        return response;
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
    default:
        break;
    }

    if (scene_.selectedCells().empty()) return response;

    auto apply = [&](auto fn) {
        for (int cid : scene_.selectedCells()) {
            fn(cid);
        }
        };

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
    default:
        return response;
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
        renderer_->uploadScene(scene_, uploadOptions_);
    }
}

void InputController::buildAndShowSelectedPath(Response& response) {
    if (renderer_) {
        if (auto poly = scene_.buildPathPolyline()) {
            renderer_->uploadPath(*poly);
        }
        else {
            renderer_->uploadPath({});
        }
    }
    response.requestUpdate = true;
}

void InputController::buildAndShowPathBetween(int startCell, int targetCell, Response& response) {
    if (renderer_) {
        PathBuilder pb(scene_.model(), pathSmoothDelta(scene_));
        pb.build();
        const auto ids = pb.astar(startCell, targetCell);
        if (!ids.empty()) {
            const auto poly = pb.polylineOnSphere(ids, kPathSegmentsPerEdge, scene_.pathBias(), scene_.heightStep());
            renderer_->uploadPath(poly);
        }
        else {
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
        if (rayTriangleMT(ro, rd, pt.v0, pt.v1, pt.v2, t) && t < bestT) {
            bestT = t;
            bestId = pt.cellId;
        }
    }
    if (bestId >= 0) return bestId;
    return std::nullopt;
}

std::optional<InputController::PickHit> InputController::pickTerrainAt(int sx, int sy) const {
    if (scene_.terrain().triOwner.empty()) return std::nullopt;
    const QVector3D ro = camera_.rayOrigin();
    const QVector3D rd = camera_.rayDirectionFromScreen(sx, sy, owner_->width(), owner_->height(), owner_->devicePixelRatioF());

    float bestT = std::numeric_limits<float>::infinity();
    int bestOwner = -1;
    QVector3D bestPos;

    const auto& P = scene_.terrain().pos;
    const auto& I = scene_.terrain().idx;
    const auto& O = scene_.terrain().triOwner;

    const size_t triCount = O.size();
    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t i0 = I[3 * t + 0];
        const uint32_t i1 = I[3 * t + 1];
        const uint32_t i2 = I[3 * t + 2];
        const QVector3D v0(P[3 * i0], P[3 * i0 + 1], P[3 * i0 + 2]);
        const QVector3D v1(P[3 * i1], P[3 * i1 + 1], P[3 * i1 + 2]);
        const QVector3D v2(P[3 * i2], P[3 * i2 + 1], P[3 * i2 + 2]);
        float tt;
        if (rayTriangleMT(ro, rd, v0, v1, v2, tt) && tt < bestT) {
            bestT = tt;
            bestOwner = O[t];
            bestPos = ro + rd * tt;
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
        const float t = (t0 > 0) ? t0 : t1;
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

    if (ecs_.get<ecs::Animation>(entity->id)) {
        response.hudMessage = QString("Explorer is already moving");
        response.requestUpdate = true;
        return;
    }

    const int oldCell = entity->currentCell;
    if (oldCell < 0 || oldCell >= scene_.model().cellCount()) return;
    if (cellId < 0 || cellId >= scene_.model().cellCount()) return;

    scene_.clearSelection();
    scene_.toggleCellSelection(oldCell);
    if (cellId != oldCell) {
        scene_.toggleCellSelection(cellId);
    }
    uploadSelection();

    if (cellId == oldCell) {
        clearPath(response);
        deselectEntity();
        response.requestUpdate = true;
        return;
    }

    buildAndShowPathBetween(oldCell, cellId, response);
    // Для Explorer-движения хотим прямую "по поверхности", без подпрыгивания
    if (!applyAnimation(entity->id, cellId, kBaseTraversalSpeed, 0.0f)) {
        if (renderer_) {
            renderer_->uploadPath({});
        }
        response.hudMessage = QString("No traversable path. Max smooth delta is %1 cells, sea cells are blocked.")
            .arg(pathClimbLimit(scene_));
        deselectEntity();
        response.requestUpdate = true;
        return;
    }

    deselectEntity();
    response.requestUpdate = true;
}

InputController::Response InputController::toggleOreVisualization() {
    oreVisualizationEnabled_ = !oreVisualizationEnabled_;
    oreAnimationTime_ = 0.0f;

    qDebug() << "Ore visualization toggled to:" << oreVisualizationEnabled_;

    Response response;
    response.requestUpdate = true;
    response.hudMessage = oreVisualizationEnabled_
        ? QString("Ore visualization: ON")
        : QString("Ore visualization: OFF");
    return response;
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
    return &scene_.modelMutable();
}

InputController::Response InputController::setOreAnimationSpeed(float speed) {
    oreAnimationSpeed_ = std::clamp(speed, 0.0f, 2.0f);

    Response response;
    response.requestUpdate = true;
    response.hudMessage = QString("Ore animation speed: %1").arg(oreAnimationSpeed_);
    return response;
}

InputController::Response InputController::regenerateOreDeposits() {
    Response response;
    response.requestUpdate = true;
    response.hudMessage = QString("Ore deposits regenerated");
    return response;
}


bool InputController::applyAnimation(int entityId, int targetCell, float speed, float bounceHeight) {
    auto* entity = ecs_.getEntity(entityId);
    if (!entity) {
        qDebug() << "Entity" << entityId << "not found for animation";
        return false;
    }

    if (ecs_.get<ecs::Animation>(entityId)) {
        return false;
    }

    auto* transform = ecs_.get<ecs::Transform>(entityId);
    if (!transform) {
        qDebug() << "Entity" << entityId << "has no transform component";
        return false;
    }

    const int startCell = entity->currentCell;
    if (startCell < 0 || startCell >= scene_.model().cellCount()) {
        qDebug() << "Entity" << entityId << "has invalid current cell:" << startCell;
        return false;
    }
    if (targetCell < 0 || targetCell >= scene_.model().cellCount()) {
        return false;
    }
    if (targetCell == startCell) {
        transform->position = computeSurfacePoint(scene_, targetCell, scene_.heightStep(), kEntitySurfaceOffset);
        return true;
    }

    PathBuilder pb(scene_.model(), pathSmoothDelta(scene_));
    pb.build();
    const auto cellPath = pb.astar(startCell, targetCell);
    if (cellPath.empty()) {
        return false;
    }

    const auto rawPathPoints = pb.polylineOnSphere(cellPath, kPathSegmentsPerEdge, scene_.pathBias(), scene_.heightStep());
    if (rawPathPoints.empty()) {
        return false;
    }

    const auto& cells = scene_.model().cells();
    const Cell& targetData = cells[static_cast<size_t>(targetCell)];
    const float entityOffsetDelta = kEntitySurfaceOffset - scene_.pathBias();

    std::vector<QVector3D> pathPoints;
    pathPoints.reserve(rawPathPoints.size());
    for (const QVector3D& point : rawPathPoints) {
        pathPoints.push_back(point.normalized() * (point.length() + entityOffsetDelta));
    }

    const QVector3D startPos = computeSurfacePoint(scene_, startCell, scene_.heightStep(), kEntitySurfaceOffset);
    const QVector3D targetPos = computeSurfacePoint(scene_, targetCell, scene_.heightStep(), kEntitySurfaceOffset);

    entity->currentCell = -1;
    transform->position = pathPoints.front();

    // ������ ��������
    ecs::Animation& anim = ecs_.emplace<ecs::Animation>(entityId);
    anim.type = ecs::Animation::Type::MoveTo;
    anim.duration = 0.0f;
    anim.elapsed = 0.0f;
    anim.startPos = startPos;
    anim.targetPos = targetPos;
    anim.startCell = startCell;
    anim.targetCell = targetCell;
    anim.pathPoints = std::move(pathPoints);
    anim.pathCumulative.assign(anim.pathPoints.size(), 0.0f);
    anim.pathTotalLength = 0.0f;

    size_t pointIndex = 0;
    for (size_t edgeIndex = 0; edgeIndex + 1 < cellPath.size() && pointIndex + 1 < anim.pathPoints.size(); ++edgeIndex) {
        const Cell& from = cells[static_cast<size_t>(cellPath[edgeIndex])];
        const Cell& to = cells[static_cast<size_t>(cellPath[edgeIndex + 1])];
        const float angularDistance = PathBuilder::edgeAngularDistance(from, to);
        const float traversalCost = pb.traversalCost(from, to);
        const float speedFactor = (angularDistance > 1e-6f && std::isfinite(traversalCost))
            ? (traversalCost / angularDistance)
            : PathBuilder::biomeTraversalFactor(to.biome);

        for (int segment = 0; segment < kPathSegmentsPerEdge && pointIndex + 1 < anim.pathPoints.size(); ++segment) {
            const QVector3D& prev = anim.pathPoints[pointIndex];
            const QVector3D& next = anim.pathPoints[pointIndex + 1];
            ++pointIndex;
            anim.pathTotalLength += (next - prev).length() * speedFactor;
            anim.pathCumulative[pointIndex] = anim.pathTotalLength;
        }
    }

    while (pointIndex + 1 < anim.pathPoints.size()) {
        const QVector3D& prev = anim.pathPoints[pointIndex];
        const QVector3D& next = anim.pathPoints[pointIndex + 1];
        ++pointIndex;
        anim.pathTotalLength += (next - prev).length();
        anim.pathCumulative[pointIndex] = anim.pathTotalLength;
    }

    if (anim.pathTotalLength <= 1e-6f) {
        anim.pathTotalLength = (targetPos - startPos).length();
        if (!anim.pathCumulative.empty()) {
            anim.pathCumulative.back() = anim.pathTotalLength;
        }
    }

    if (anim.pathPoints.size() >= 2) {
        const QVector3D unitUp = startPos.normalized();
        QVector3D edge = anim.pathPoints[1] - anim.pathPoints[0];
        QVector3D t = edge - QVector3D::dotProduct(edge, unitUp) * unitUp;
        if (t.length() < 1e-6f) {
            QVector3D refX(1, 0, 0);
            QVector3D right = refX - QVector3D::dotProduct(refX, unitUp) * unitUp;
            if (right.length() < 0.01f) {
                refX = QVector3D(0, 0, 1);
                right = refX - QVector3D::dotProduct(refX, unitUp) * unitUp;
            }
            right.normalize();
            anim.surfaceForward = QVector3D::crossProduct(unitUp, right).normalized();
        }
        else {
            anim.surfaceForward = t.normalized();
        }
    }
    // Сохраняем направление сразу в Transform, чтобы оно не зависело от наличия Animation.
    transform->surfaceForward = anim.surfaceForward;

    speed = std::max(0.01f, speed);
    // Если caller передал 0.0f, подпрыгивание должно полностью отключиться.
    anim.bounceHeight = std::clamp(bounceHeight, 0.0f, bounceHeightForBiome(targetData.biome));
    anim.arcPeakT = arcPeakForBiome(targetData.biome);
    anim.softLanding = usesSoftLanding(targetData.biome);
    anim.duration = std::max(0.1f, (anim.pathTotalLength / speed) * landingDurationMultiplier(targetData.biome));
    anim.rotationSpeed = 540.0f;

    anim.onComplete = [this, targetCell, targetPos](int id) {
        if (auto* completedEntity = ecs_.getEntity(id)) {
            completedEntity->currentCell = targetCell;
        }
        if (auto* completedTransform = ecs_.get<ecs::Transform>(id)) {
            completedTransform->position = targetPos;
        }
        };

    return true;
}

void InputController::updateAnimations(float dt) {
    ecs_.update(dt);
}
