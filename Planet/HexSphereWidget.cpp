#include "HexSphereWidget.h"

#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QtDebug>
#include <cmath>
#include <limits>

#include "scene/Transform.h"
#include "SurfacePlacement.h"

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

HexSphereWidget::HexSphereWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    auto* hud = new QLabel(this);
    hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    hud->setStyleSheet("QLabel { background: rgba(0,0,0,140); color: white; padding: 6px; }");
    hud->move(10, 10);
    hud->setText("LMB: select | C: clear path | P: build path | +/-: height | 1-8: biomes | S: smooth | W: move");
    hud->adjustSize();

    waterTimer_ = new QTimer(this);
    connect(waterTimer_, &QTimer::timeout, this, [this]() {
        waterTime_ += 0.016f;
        update();
    });
}

HexSphereWidget::~HexSphereWidget() = default;

void HexSphereWidget::initializeGL() {
    makeCurrent();

    QOpenGLContext* ctx = context();

    if (!ctx) qFatal("No OpenGL context!");

    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(ctx);
    if (!gl) {
        qFatal("Cannot obtain QOpenGLFunctions_3_3_Core");
    }

    gl->initializeOpenGLFunctions();

    //что за карточка используется
    printGlInfo(gl);

    renderer_.initialize(this, gl, &stats_);

    waterTimer_->start(16);

    SceneEntity pyramid("Explorer", "pyramid");
    pyramid.setCurrentCell(0);
    QVector3D surfacePosition = computeSurfacePoint(scene_, 0);
    pyramid.transform().position = scene::localToWorldPoint(pyramid.transform(), scene::CoordinateFrame{}, surfacePosition);
    pyramid.attachCollider(std::make_unique<scene::SphereCollider>(0.08f));
    pyramid.setSelected(true);
    sceneGraph_.addEntity(pyramid);

    rebuildModel();

    emit hudTextChanged("Controls: [LMB] select | [C] clear path | [P] path between selected | [+/-] height | [1-8] biomes | [S] smooth toggle | [W] move entity");
}

void HexSphereWidget::resizeGL(int w, int h) {
    renderer_.resize(w, h, devicePixelRatioF(), proj_);
}

void HexSphereWidget::paintGL() {

    updateCamera();
    renderer_.render(view_, proj_, scene_, sceneGraph_, waterTime_, lightDir_, selectedEntityId_, scene_.heightStep());

    stats_.frameRendered();
}

void HexSphereWidget::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    lastPos_ = e->pos();

    if (e->button() == Qt::RightButton) {
        rotating_ = true;
    }
    else if (e->button() == Qt::LeftButton) {
        auto hit = pickSceneAt(e->pos().x(), e->pos().y());
        if (hit) {
            if (hit->isEntity) {
                selectEntity(hit->entityId);
            }
            else if (selectedEntityId_ != -1) {
                moveSelectedEntityToCell(hit->cellId);
                deselectEntity();
            }
            else {
                scene_.toggleCellSelection(hit->cellId);
                uploadSelection();
            }
            update();
        }
        else {
            deselectEntity();
            update();
        }
    }
}

void HexSphereWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!rotating_) return;

    const QPoint currentPos = e->pos();
    const QPoint delta = currentPos - lastPos_;
    lastPos_ = currentPos;

    if (delta.manhattanLength() == 0) return;

    const float sensitivity = 0.002f;
    QQuaternion rotationX = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), delta.x() * sensitivity * 180.0f);
    QQuaternion rotationY = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), delta.y() * sensitivity * 180.0f);
    QQuaternion rotation = rotationY * rotationX;
    sphereRotation_ = rotation * sphereRotation_;

    update();
}

void HexSphereWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::RightButton) {
        rotating_ = false;
    }
}

void HexSphereWidget::wheelEvent(QWheelEvent* e) {
    const float steps = (e->angleDelta().y() / 8.0f) / 15.0f;
    distance_ *= std::pow(0.9f, steps);
    distance_ = std::clamp(distance_, 1.2f, 10.0f);
    update();
}

void HexSphereWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_C:
        clearPath();
        return;
    case Qt::Key_S:
        scene_.setSmoothOneStep(!scene_.smoothOneStep());
        rebuildModel();
        emit hudTextChanged("Smooth mode: " + QString(scene_.smoothOneStep() ? "ON" : "OFF"));
        return;
    case Qt::Key_P:
        buildAndShowSelectedPath();
        return;
    case Qt::Key_W:
    {
        auto sel = sceneGraph_.getSelectedEntity();
        if (!sel) return;
        SceneEntity& ent = sel->get();
        const auto& cells = scene_.model().cells();
        if (ent.currentCell() < 0 || ent.currentCell() >= (int)cells.size()) return;
        const auto& c = cells[(size_t)ent.currentCell()];
        if (c.neighbors.empty()) return;
        int next = c.neighbors[0];
        if (next < 0) return;
        ent.setCurrentCell(next);
        ent.transform().position = computeSurfacePoint(scene_, next);
        update();
        break;
    }
    case Qt::Key_Escape:
        deselectEntity();
        update();
        return;
    case Qt::Key_Delete:
        if (selectedEntityId_ != -1) {
            sceneGraph_.removeEntity(selectedEntityId_);
            selectedEntityId_ = -1;
            update();
        }
        return;
    default: break;
    }

    if (scene_.selectedCells().empty()) return;

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
    default: return;
    }
    rebuildModel();
}

void HexSphereWidget::setSubdivisionLevel(int L) {
    if (scene_.subdivisionLevel() != L) {
        scene_.setSubdivisionLevel(L);
        stats_.setSubdivisionLevel(L);
        updateBufferUsageStrategy();
        rebuildModel();
        update();
    }
}

void HexSphereWidget::resetView() {
    distance_ = 2.2f;
    sphereRotation_ = QQuaternion();
    update();
}

void HexSphereWidget::clearSelection() {
    scene_.clearSelection();
    uploadSelection();
    update();
}

void HexSphereWidget::regenerateTerrain() {
    scene_.regenerateTerrain();
    rebuildModel();
}

void HexSphereWidget::updateCamera() {
    view_.setToIdentity();
    const QVector3D eye = QVector3D(0, 0, distance_);
    const QVector3D center = QVector3D(0, 0, 0);
    const QVector3D up = QVector3D(0, 1, 0);
    view_.lookAt(eye, center, up);
    view_.rotate(sphereRotation_);
}

QVector3D HexSphereWidget::rayOrigin() const {
    const QMatrix4x4 invView = view_.inverted();
    return (invView.map(QVector4D(0, 0, 0, 1))).toVector3D();
}

QVector3D HexSphereWidget::rayDirectionFromScreen(int sx, int sy) const {
    const float dpr = devicePixelRatioF();
    const float w = float(width() * dpr);
    const float h = float(height() * dpr);
    const float x = 2.0f * (float(sx) * dpr / w) - 1.0f;
    const float y = 1.0f - 2.0f * (float(sy) * dpr / h);
    const QMatrix4x4 inv = (proj_ * view_).inverted();
    QVector4D pNear = inv.map(QVector4D(x, y, -1.0f, 1.0f));
    QVector4D pFar = inv.map(QVector4D(x, y, 1.0f, 1.0f));
    pNear /= pNear.w();
    pFar /= pFar.w();
    return (pFar.toVector3D() - pNear.toVector3D()).normalized();
}

std::optional<int> HexSphereWidget::pickCellAt(int sx, int sy) {
    const QVector3D ro = rayOrigin();
    const QVector3D rd = rayDirectionFromScreen(sx, sy);
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

std::optional<HexSphereWidget::PickHit> HexSphereWidget::pickTerrainAt(int sx, int sy) const {
    if (scene_.terrain().triOwner.empty()) return std::nullopt;
    const QVector3D ro = rayOrigin();
    const QVector3D rd = rayDirectionFromScreen(sx, sy);

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

std::optional<HexSphereWidget::PickHit> HexSphereWidget::pickEntityAt(int sx, int sy) const {
    const QVector3D ro = rayOrigin();
    const QVector3D rd = rayDirectionFromScreen(sx, sy);

    float bestT = std::numeric_limits<float>::infinity();
    int bestEntityId = -1;
    QVector3D bestPos;

    for (const auto& e : sceneGraph_.entities()) {
        if (!e->collider()) continue;

        const scene::SphereCollider* sphere =
            dynamic_cast<const scene::SphereCollider*>(e->collider());
        if (!sphere) continue;

        const QVector3D center = e->transform().position;
        const float radius = sphere->radius();
        const QVector3D oc = ro - center;
        const float b = 2.0f * QVector3D::dotProduct(oc, rd);
        const float c = QVector3D::dotProduct(oc, oc) - radius * radius;
        const float discriminant = b * b - 4.0f * c;
        if (discriminant < 0) continue;
        const float sqrtDisc = std::sqrt(discriminant);
        const float t0 = (-b - sqrtDisc) * 0.5f;
        const float t1 = (-b + sqrtDisc) * 0.5f;
        float t = (t0 > 0) ? t0 : t1;
        if (t > 0 && t < bestT) {
            bestT = t;
            bestEntityId = e->id();
            bestPos = ro + rd * t;
        }
    }

    if (bestEntityId != -1) {
        return PickHit{ -1, bestEntityId, bestPos, bestT, true };
    }
    return std::nullopt;
}

std::optional<HexSphereWidget::PickHit> HexSphereWidget::pickSceneAt(int sx, int sy) const {
    auto entityHit = pickEntityAt(sx, sy);
    auto terrainHit = pickTerrainAt(sx, sy);

    if (entityHit && terrainHit) {
        return (entityHit->t < terrainHit->t) ? entityHit : terrainHit;
    }
    return entityHit ? entityHit : terrainHit;
}

void HexSphereWidget::selectEntity(int entityId) {
    if (selectedEntityId_ == entityId) return;

    if (auto previous = sceneGraph_.getEntity(selectedEntityId_)) {
        previous->get().setSelected(false);
    }

    selectedEntityId_ = entityId;
    if (auto entityOpt = sceneGraph_.getEntity(entityId)) {
        entityOpt->get().setSelected(true);
    }
    update();
}

void HexSphereWidget::deselectEntity() {
    if (selectedEntityId_ != -1) {
        if (auto entityOpt = sceneGraph_.getEntity(selectedEntityId_)) {
            entityOpt->get().setSelected(false);
        }
        selectedEntityId_ = -1;
    }
}

void HexSphereWidget::moveSelectedEntityToCell(int cellId) {
    if (selectedEntityId_ == -1) return;
    auto entityOpt = sceneGraph_.getEntity(selectedEntityId_);
    if (!entityOpt) return;

    SceneEntity& entity = entityOpt->get();
    if (cellId >= 0 && cellId < scene_.model().cellCount()) {
        entity.setCurrentCell(cellId);
        entity.transform().position = computeSurfacePoint(scene_, cellId);
    }
    update();
}

void HexSphereWidget::rebuildModel() {
    scene_.rebuildModel();
    uploadBuffers();
    update();
}

void HexSphereWidget::uploadSelection() {
    renderer_.uploadSelectionOutline(scene_.buildSelectionOutlineVertices());
}

void HexSphereWidget::uploadBuffers() {
    renderer_.uploadScene(scene_, uploadOptions_);
}

void HexSphereWidget::buildAndShowSelectedPath() {
    if (auto poly = scene_.buildPathPolyline()) {
        renderer_.uploadPath(*poly);
        update();
    }
}

void HexSphereWidget::clearPath() {
    renderer_.uploadPath({});
    update();
}

void HexSphereWidget::updateBufferUsageStrategy() {
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

