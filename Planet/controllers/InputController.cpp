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
#include <utility>

#include "controllers/CameraController.h"
#include "controllers/PathBuilder.h"
#include "dag/EngineFacade.h"
#include "ECS/Transform.h"
#include "model/SurfacePlacement.h"

namespace {

    constexpr int kPathSegmentsPerEdge = 8;
    constexpr float kEntitySurfaceOffset = 0.0f;
    constexpr float kBaseTraversalSpeed = 0.35f;
    constexpr const char* kFactoryMeshId = "factory";
    constexpr const char* kMineMeshId = "mine";

    QString placementModelName(InputController::PlacementModel model) {
        switch (model) {
        case InputController::PlacementModel::Factory:
            return QString("Factory");
        case InputController::PlacementModel::Mine:
            return QString("Mine");
        case InputController::PlacementModel::Delete:
            return QString("Delete");
        case InputController::PlacementModel::None:
        default:
            return QString("Building");
        }
    }

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

    std::optional<std::pair<int, int>> selectedPathEndpoints(const HexSphereSceneController& scene) {
        const QSet<int>& selected = scene.selectedCells();
        if (selected.size() != 2) {
            return std::nullopt;
        }

        auto it = selected.begin();
        const int startCell = *it;
        ++it;
        const int goalCell = *it;
        return std::make_pair(startCell, goalCell);
    }

    bool isMovableEntity(const ecs::ComponentStorage& ecs, int entityId) {
        const auto* mesh = ecs.get<ecs::Mesh>(entityId);
        return !mesh || (mesh->meshId != kFactoryMeshId && mesh->meshId != kMineMeshId);
    }

    bool isExplorerEntity(const ecs::ComponentStorage& ecs, int entityId) {
        const auto* mesh = ecs.get<ecs::Mesh>(entityId);
        return mesh && (mesh->meshId == "pyramid" || mesh->meshId == "car");
    }

    int chooseInitialExplorerCell(const HexSphereSceneController& scene) {
        const auto& cells = scene.model().cells();
        if (cells.empty()) {
            return 0;
        }

        int bestCell = 0;
        float bestZ = cells.front().centroid.z();
        for (size_t i = 1; i < cells.size(); ++i) {
            const float z = cells[i].centroid.z();
            if (z > bestZ) {
                bestZ = z;
                bestCell = static_cast<int>(i);
            }
        }
        return bestCell;
    }

} // namespace

InputController::InputController(CameraController& camera, SceneViewMode viewMode)
    : camera_(camera)
    , viewMode_(viewMode)
    , scene_(viewMode) {
}

InputController::~InputController() {
    renderer_.reset();
    ecs_.clear();
    scene_.clearForShutdown();
    engine_ = nullptr;
    owner_ = nullptr;
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

    if (!isContributorMode()) {
        const int startCell = chooseInitialExplorerCell(scene_);
        auto& explorer = ecs_.createEntity("Explorer");
        explorer.currentCell = startCell;
        ecs_.emplace<ecs::Mesh>(explorer.id).meshId = "car";
        ecs::Transform& transform = ecs_.emplace<ecs::Transform>(explorer.id);
        const QVector3D surfacePosition = computeSurfacePoint(scene_, startCell, scene_.heightStep(), kEntitySurfaceOffset);
        transform.position = ecs::localToWorldPoint(transform, ecs::CoordinateFrame{}, surfacePosition);
        ecs_.emplace<ecs::Collider>(explorer.id).radius = 0.20f;
        qDebug() << "Explorer car initialized on visible cell" << startCell << "at" << surfacePosition;
    }

    Response initResponse;
    rebuildModel(initResponse);
    refreshBuildPreview();
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

    if (isContributorMode()) {
        return response;
    }

    if (e->button() == Qt::LeftButton) {
        const auto p = e->position();
        auto hit = pickSceneAt(p.x(), p.y());
        if (!hit) return response;

        if (isPlacementModeActive()) {
            if (placementModel_ == PlacementModel::Delete) {
                return deleteEntityAtHit(*hit);
            }
            if (hit->isEntity) {
                response.hudMessage = QString("Selected cell is occupied");
                response.requestUpdate = true;
                return response;
            }
            return placeBuildingOnCell(hit->cellId);
        }

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
    switch (e->key()) {
    case Qt::Key_C:
        return executeCommand(SceneCommand::ClearPath);
    case Qt::Key_O:
        return executeCommand(SceneCommand::ToggleOreVisualization);
    case Qt::Key_S:
        return executeCommand(SceneCommand::ToggleSmooth);
    case Qt::Key_P:
        return executeCommand(SceneCommand::BuildPath);
    case Qt::Key_W:
        return executeCommand(SceneCommand::MoveSelectedEntity);
    case Qt::Key_Escape:
        return executeCommand(SceneCommand::DeselectEntity);
    case Qt::Key_Delete:
        return executeCommand(SceneCommand::DeleteSelectedEntity);
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        return executeCommand(SceneCommand::IncreaseHeight);
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        return executeCommand(SceneCommand::DecreaseHeight);
    case Qt::Key_1:
        return executeCommand(SceneCommand::SetBiomeSea);
    case Qt::Key_2:
        return executeCommand(SceneCommand::SetBiomeGrass);
    case Qt::Key_3:
        return executeCommand(SceneCommand::SetBiomeRock);
    case Qt::Key_4:
        return executeCommand(SceneCommand::SetBiomeSnow);
    case Qt::Key_5:
        return executeCommand(SceneCommand::SetBiomeTundra);
    case Qt::Key_6:
        return executeCommand(SceneCommand::SetBiomeDesert);
    case Qt::Key_7:
        return executeCommand(SceneCommand::SetBiomeSavanna);
    case Qt::Key_8:
        return executeCommand(SceneCommand::SetBiomeJungle);
    default:
        return {};
    }
}

InputController::Response InputController::executeCommand(SceneCommand command) {
    Response response;
    if (isContributorMode()) {
        return contributorModeResponse();
    }

    auto selectedEntity = [&]() -> ecs::Entity* {
        auto selected = ecs_.selectedEntity();
        return selected ? &selected->get() : nullptr;
    };

    auto requireSelectedCells = [&]() -> bool {
        if (!scene_.selectedCells().empty()) {
            return true;
        }
        response.hudMessage = QString("Select one or more cells first");
        response.requestUpdate = true;
        return false;
    };

    auto applyToSelectedCells = [&](auto fn) {
        for (int cid : scene_.selectedCells()) {
            fn(cid);
        }
    };

    auto setSelectedBiome = [&](Biome biome, const QString& name) {
        if (!requireSelectedCells()) {
            return;
        }
        applyToSelectedCells([&](int cid) { scene_.modelMutable().setBiome(cid, biome); });
        rebuildDerivedGeometry(response);
        response.hudMessage = QString("Biome: %1").arg(name);
    };

    switch (command) {
    case SceneCommand::ClearPath:
        clearPath(response);
        response.hudMessage = QString("Path cleared");
        return response;
    case SceneCommand::ToggleOreVisualization:
        return toggleOreVisualization();
    case SceneCommand::ToggleSmooth:
        scene_.setSmoothOneStep(!scene_.smoothOneStep());
        if (engine_) {
            engine_->setPathSmoothMaxDelta(pathSmoothDelta(scene_));
        }
        rebuildDerivedGeometry(response);
        response.hudMessage = QString("Smooth mode: ") + (scene_.smoothOneStep() ? "ON" : "OFF");
        return response;
    case SceneCommand::BuildPath:
        if (scene_.selectedCells().size() != 2) {
            if (renderer_) {
                renderer_->uploadPath({});
            }
            response.hudMessage = QString("Select exactly two cells to build a path");
            response.requestUpdate = true;
            return response;
        }
        buildAndShowSelectedPath(response);
        return response;
    case SceneCommand::MoveSelectedEntity:
    {
        ecs::Entity* entity = selectedEntity();
        if (!entity) {
            response.hudMessage = QString("Select Explorer first");
            response.requestUpdate = true;
            return response;
        }
        if (!isMovableEntity(ecs_, entity->id)) {
            response.hudMessage = QString("Static building cannot be moved");
            response.requestUpdate = true;
            return response;
        }
        if (ecs_.get<ecs::Animation>(entity->id)) {
            response.hudMessage = QString("Explorer is already moving");
            response.requestUpdate = true;
            return response;
        }

        const auto& cells = scene_.model().cells();
        if (entity->currentCell < 0 || entity->currentCell >= static_cast<int>(cells.size())) {
            response.hudMessage = QString("Explorer is not on a valid cell");
            response.requestUpdate = true;
            return response;
        }
        const auto& currentCell = cells[static_cast<size_t>(entity->currentCell)];
        if (currentCell.neighbors.empty()) {
            response.hudMessage = QString("No neighboring cells");
            response.requestUpdate = true;
            return response;
        }

        bool moved = false;
        for (int next : currentCell.neighbors) {
            if (next < 0) continue;
            buildAndShowPathBetween(entity->currentCell, next, response);
            moved = applyAnimation(entity->id, next, kBaseTraversalSpeed, 0.0f);
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
    case SceneCommand::DeselectEntity:
        deselectEntity();
        response.requestUpdate = true;
        return response;
    case SceneCommand::DeleteSelectedEntity:
        if (selectedEntityId_ != -1) {
            ecs_.destroyEntity(selectedEntityId_);
            selectedEntityId_ = -1;
            response.requestUpdate = true;
            response.hudMessage = QString("Selected entity deleted");
        }
        return response;
    case SceneCommand::IncreaseHeight:
        if (!requireSelectedCells()) {
            return response;
        }
        applyToSelectedCells([&](int cid) { scene_.modelMutable().addHeight(cid, +1); });
        rebuildDerivedGeometry(response);
        response.hudMessage = QString("Height +1");
        return response;
    case SceneCommand::DecreaseHeight:
        if (!requireSelectedCells()) {
            return response;
        }
        applyToSelectedCells([&](int cid) { scene_.modelMutable().addHeight(cid, -1); });
        rebuildDerivedGeometry(response);
        response.hudMessage = QString("Height -1");
        return response;
    case SceneCommand::SetBiomeSea:
        setSelectedBiome(Biome::Sea, "Sea");
        return response;
    case SceneCommand::SetBiomeGrass:
        setSelectedBiome(Biome::Grass, "Grass");
        return response;
    case SceneCommand::SetBiomeRock:
        setSelectedBiome(Biome::Rock, "Rock");
        return response;
    case SceneCommand::SetBiomeSnow:
        setSelectedBiome(Biome::Snow, "Snow");
        return response;
    case SceneCommand::SetBiomeTundra:
        setSelectedBiome(Biome::Tundra, "Tundra");
        return response;
    case SceneCommand::SetBiomeDesert:
        setSelectedBiome(Biome::Desert, "Desert");
        return response;
    case SceneCommand::SetBiomeSavanna:
        setSelectedBiome(Biome::Savanna, "Savanna");
        return response;
    case SceneCommand::SetBiomeJungle:
        setSelectedBiome(Biome::Jungle, "Jungle");
        return response;
    }

    return response;
}

InputController::Response InputController::setSubdivisionLevel(int L) {
    Response response;
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    if (scene_.subdivisionLevel() != L) {
        stats_.setSubdivisionLevel(L);
        updateBufferUsageStrategy(L);
        if (engine_) {
            engine_->setSubdivisionLevel(L);
            const auto result = engine_->regenerateTerrain();
            if (!result) {
                response.hudMessage = QString::fromStdString(result.message);
                return response;
            }
        }
        else {
            scene_.setSubdivisionLevel(L);
        }
        refreshEntityTransformsForTerrain();
        refreshBuildPreview();
        uploadBuffers();
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
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    scene_.clearSelection();
    uploadSelection();
    response.requestUpdate = true;
    return response;
}

InputController::Response InputController::setTerrainParams(const TerrainParams& p) {
    Response response;
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    if (engine_) {
        engine_->setTerrainParams(p);
    }
    else {
        scene_.setGenParams(p);
    }
    return response;
}

InputController::Response InputController::setGeneratorByIndex(int idx) {
    Response response;
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    if (engine_) {
        engine_->setGeneratorByIndex(idx);
    }
    else {
        scene_.setGeneratorByIndex(idx);
    }
    return response;
}

InputController::Response InputController::regenerateTerrain() {
    Response response;
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    if (engine_) {
        const auto result = engine_->regenerateTerrain();
        if (!result) {
            response.hudMessage = QString::fromStdString(result.message);
            return response;
        }
    }
    else {
        scene_.regenerateTerrain();
    }
    refreshEntityTransformsForTerrain();
    refreshBuildPreview();
    uploadBuffers();
    response.requestUpdate = true;
    return response;
}

InputController::Response InputController::setSmoothOneStep(bool on) {
    Response response;
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    scene_.setSmoothOneStep(on);
    if (engine_) {
        engine_->setPathSmoothMaxDelta(pathSmoothDelta(scene_));
    }
    rebuildDerivedGeometry(response);
    return response;
}

InputController::Response InputController::setStripInset(float v) {
    Response response;
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    scene_.setStripInset(v);
    rebuildDerivedGeometry(response);
    return response;
}

InputController::Response InputController::setOutlineBias(float v) {
    Response response;
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    scene_.setOutlineBias(v);
    rebuildDerivedGeometry(response);
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
    refreshEntityTransformsForTerrain();
    syncPathBackendFromScene();
    uploadBuffers();
    refreshBuildPreview();
    response.requestUpdate = true;
}

void InputController::rebuildDerivedGeometry(Response& response) {
    scene_.rebuildDerivedGeometry();
    refreshEntityTransformsForTerrain();
    syncPathBackendFromScene();
    uploadBuffers();
    refreshBuildPreview();
    response.requestUpdate = true;
}

void InputController::uploadSelection() {
    refreshSceneDagOutputs();
    if (renderer_) {
        const auto vertices = isBuildingPlacementMode()
            ? scene_.buildOutlineVerticesForCells(buildPreviewCells_)
            : scene_.buildSelectionOutlineVertices();
        renderer_->uploadSelectionOutline(vertices);
    }
}

void InputController::uploadBuffers() {
    refreshSceneDagOutputs();
    if (renderer_) {
        renderer_->uploadScene(scene_, uploadOptions_);
    }
}

void InputController::refreshSceneDagOutputs() {
    if (!engine_ || isContributorMode()) {
        return;
    }

    SceneDagRequest request;
    request.terrain = scene_.captureTerrainSnapshot();
    request.heightStep = scene_.heightStep();
    request.outlineBias = scene_.outlineBias();
    request.smoothOneStep = scene_.smoothOneStep();
    request.selectedCells.reserve(static_cast<size_t>(scene_.selectedCells().size()));
    for (int cellId : scene_.selectedCells()) {
        request.selectedCells.push_back(cellId);
    }
    std::sort(request.selectedCells.begin(), request.selectedCells.end());

    ecs_.each<ecs::Mesh, ecs::Transform>([&](const ecs::Entity& entity, const ecs::Mesh& mesh, const ecs::Transform&) {
        ModelPlacementRequest placement;
        placement.entityId = entity.id;
        placement.meshId = mesh.meshId;
        placement.cellId = entity.currentCell;
        placement.selected = entity.selected;
        placement.surfaceOffset = kEntitySurfaceOffset;
        request.modelRequests.push_back(std::move(placement));
        });

    SceneDagResult result = engine_->rebuildSceneDerived(request);

    // Keep the legacy selection outline path as a fallback so the UI does not
    // lose cell highlighting if the scene DAG skips or returns an empty result.
    if (request.selectedCells.empty() || !result.selectionOutline.vertices.empty()) {
        scene_.setSelectionOutlineVertices(std::move(result.selectionOutline.vertices));
    }
    if (!result.treePlacements.empty() || scene_.getTreePlacements().empty()) {
        scene_.setTreePlacements(std::move(result.treePlacements));
    }
}

void InputController::syncPathBackendFromScene() {
    if (!engine_ || isContributorMode()) {
        return;
    }
    engine_->setPathTerrainSnapshot(scene_.captureTerrainSnapshot());
}

void InputController::refreshEntityTransformsForTerrain() {
    if (isContributorMode()) {
        return;
    }

    const int cellCount = scene_.model().cellCount();
    for (const auto& entityRef : ecs_.entities()) {
        const ecs::Entity& entity = entityRef.get();
        if (entity.currentCell < 0 || entity.currentCell >= cellCount) {
            continue;
        }

        if (auto* transform = ecs_.get<ecs::Transform>(entity.id)) {
            transform->position = computeSurfacePoint(scene_, entity.currentCell, scene_.heightStep(), kEntitySurfaceOffset);
        }
    }
}

void InputController::stageTerrainParams(const TerrainParams& params) {
    scene_.setGenParams(params);
}

void InputController::stageGeneratorByIndex(int idx) {
    scene_.setGeneratorByIndex(idx);
}

void InputController::stageSubdivisionLevel(int level) {
    scene_.stageSubdivisionLevel(level);
}

void InputController::rebuildTerrainFromInputs() {
    if (isContributorMode()) {
        return;
    }
    scene_.rebuildTerrainFromInputs();
}

TerrainSnapshot InputController::captureTerrainSnapshot() const {
    return scene_.captureTerrainSnapshot();
}

void InputController::projectTerrainSnapshot(const TerrainSnapshot& snapshot) {
    if (isContributorMode()) {
        return;
    }
    scene_.applyTerrainSnapshot(snapshot);
    refreshEntityTransformsForTerrain();
    refreshBuildPreview();
    syncPathBackendFromScene();
}

void InputController::buildAndShowSelectedPath(Response& response) {
    if (renderer_) {
        if (auto endpoints = selectedPathEndpoints(scene_)) {
            const PathResult result = engine_
                ? engine_->findPath(endpoints->first, endpoints->second)
                : PathResult{};
            renderer_->uploadPath(scene_.buildPathPolyline(result.cellIds));
        }
        else {
            renderer_->uploadPath({});
        }
    }
    response.requestUpdate = true;
}

void InputController::buildAndShowPathBetween(int startCell, int targetCell, Response& response) {
    if (renderer_) {
        const PathResult result = engine_
            ? engine_->findPath(startCell, targetCell)
            : PathResult{};
        renderer_->uploadPath(scene_.buildPathPolyline(result.cellIds));
    }
    response.requestUpdate = true;
}

void InputController::clearPath(Response& response) {
    if (renderer_) {
        renderer_->uploadPath({});
    }
    response.requestUpdate = true;
}

void InputController::updateBufferUsageStrategy(int subdivisionLevel) {
    const int L = subdivisionLevel;
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
    if (!isMovableEntity(ecs_, entity->id)) {
        response.hudMessage = QString("Static building cannot be moved");
        response.requestUpdate = true;
        return;
    }

    if (ecs_.get<ecs::Animation>(entity->id)) {
        response.hudMessage = QString("Explorer is already moving");
        response.requestUpdate = true;
        return;
    }

    const int oldCell = entity->currentCell;
    if (oldCell < 0 || oldCell >= scene_.model().cellCount()) return;
    if (cellId < 0 || cellId >= scene_.model().cellCount()) return;
    if (isCellOccupied(cellId, entity->id)) {
        response.hudMessage = QString("Target cell is occupied");
        response.requestUpdate = true;
        return;
    }

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
    // Р вЂќР В»РЎРЏ Explorer-Р Т‘Р Р†Р С‘Р В¶Р ВµР Р…Р С‘РЎРЏ РЎвЂ¦Р С•РЎвЂљР С‘Р С Р С—РЎР‚РЎРЏР СРЎС“РЎР‹ "Р С—Р С• Р С—Р С•Р Р†Р ВµРЎР‚РЎвЂ¦Р Р…Р С•РЎРѓРЎвЂљР С‘", Р В±Р ВµР В· Р С—Р С•Р Т‘Р С—РЎР‚РЎвЂ№Р С–Р С‘Р Р†Р В°Р Р…Р С‘РЎРЏ
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

bool InputController::isCellOccupied(int cellId, std::optional<int> ignoredEntityId) const {
    for (const auto& entityRef : ecs_.entities()) {
        const ecs::Entity& entity = entityRef.get();
        if (ignoredEntityId && entity.id == *ignoredEntityId) {
            continue;
        }
        if (entity.currentCell == cellId) {
            return true;
        }
    }
    return false;
}

InputController::Response InputController::placeBuildingOnCell(int cellId) {
    Response response;
    if (placementModel_ == PlacementModel::None || placementModel_ == PlacementModel::Delete) {
        return response;
    }
    if (cellId < 0 || cellId >= scene_.model().cellCount()) {
        response.hudMessage = QString("Invalid target cell");
        response.requestUpdate = true;
        return response;
    }
    if (isCellOccupied(cellId)) {
        response.hudMessage = QString("Selected cell is occupied");
        response.requestUpdate = true;
        return response;
    }
    if (!canBuildOnCell(cellId)) {
        response.hudMessage = QString("You can build only on cells next to the explorer");
        response.requestUpdate = true;
        return response;
    }

    auto& building = ecs_.createEntity(placementModelName(placementModel_));
    building.currentCell = cellId;

    const char* meshId = placementModel_ == PlacementModel::Factory ? kFactoryMeshId : kMineMeshId;
    ecs_.emplace<ecs::Mesh>(building.id).meshId = meshId;

    ecs::Transform& transform = ecs_.emplace<ecs::Transform>(building.id);
    transform.position = computeSurfacePoint(scene_, cellId, scene_.heightStep(), kEntitySurfaceOffset);
    ecs_.emplace<ecs::Collider>(building.id).radius = 0.16f;

    response.hudMessage = QString("%1 placed on cell %2")
        .arg(placementModelName(placementModel_))
        .arg(cellId);
    response.requestUpdate = true;
    refreshBuildPreview();
    return response;
}

bool InputController::isDeletableEntity(int entityId) const {
    const auto* mesh = ecs_.get<ecs::Mesh>(entityId);
    if (!mesh) {
        return false;
    }
    return mesh->meshId == kFactoryMeshId || mesh->meshId == kMineMeshId;
}

InputController::Response InputController::deleteEntityAtHit(const PickHit& hit) {
    Response response;
    int entityId = -1;

    if (hit.isEntity) {
        entityId = hit.entityId;
    }
    else if (hit.cellId >= 0) {
        for (const auto& entityRef : ecs_.entities()) {
            const ecs::Entity& entity = entityRef.get();
            if (entity.currentCell == hit.cellId && isDeletableEntity(entity.id)) {
                entityId = entity.id;
                break;
            }
        }
    }

    if (entityId == -1 || !isDeletableEntity(entityId)) {
        response.hudMessage = QString("Click a factory or mine to delete it");
        response.requestUpdate = true;
        return response;
    }

    if (selectedEntityId_ == entityId) {
        deselectEntity();
    }
    ecs_.destroyEntity(entityId);
    response.hudMessage = QString("Building removed");
    response.requestUpdate = true;
    refreshBuildPreview();
    return response;
}

InputController::Response InputController::contributorModeResponse() const {
    Response response;
    response.requestUpdate = true;
    response.hudMessage = QString("Contributor mode: camera only");
    return response;
}

InputController::Response InputController::toggleOreVisualization() {
    if (isContributorMode()) {
        return contributorModeResponse();
    }
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
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    oreAnimationSpeed_ = std::clamp(speed, 0.0f, 2.0f);

    Response response;
    response.requestUpdate = true;
    response.hudMessage = QString("Ore animation speed: %1").arg(oreAnimationSpeed_);
    return response;
}

InputController::Response InputController::regenerateOreDeposits() {
    if (isContributorMode()) {
        return contributorModeResponse();
    }
    Response response;
    response.requestUpdate = true;
    response.hudMessage = QString("Ore deposits regenerated");
    return response;
}

InputController::Response InputController::setPlacementModel(PlacementModel model) {
    if (isContributorMode()) {
        return contributorModeResponse();
    }

    Response response;
    placementModel_ = model;
    refreshBuildPreview();
    response.requestUpdate = true;
    if (placementModel_ == PlacementModel::None) {
        response.hudMessage = QString("Building placement disabled");
    }
    else if (placementModel_ == PlacementModel::Delete) {
        response.hudMessage = QString("Delete mode enabled. Click a factory or mine to remove it.");
    }
    else {
        response.hudMessage = QString("%1 placement enabled. Click a cell on the planet.")
            .arg(placementModelName(placementModel_));
    }
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
    if (targetCell != startCell && isCellOccupied(targetCell, entityId)) {
        qDebug() << "Entity" << entityId << "cannot move to occupied cell" << targetCell;
        return false;
    }
    if (targetCell == startCell) {
        transform->position = computeSurfacePoint(scene_, targetCell, scene_.heightStep(), kEntitySurfaceOffset);
        refreshBuildPreview();
        return true;
    }

    const PathResult result = engine_
        ? engine_->findPath(startCell, targetCell)
        : PathResult{};
    const auto& cellPath = result.cellIds;
    if (cellPath.empty()) {
        return false;
    }

    PathBuilder pb(scene_.model(), pathSmoothDelta(scene_));
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

    // РїС—Р…РїС—Р…РїС—Р…РїС—Р…РїС—Р…РїС—Р… РїС—Р…РїС—Р…РїС—Р…РїС—Р…РїС—Р…РїС—Р…РїС—Р…РїС—Р…
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
    // Р РЋР С•РЎвЂ¦РЎР‚Р В°Р Р…РЎРЏР ВµР С Р Р…Р В°Р С—РЎР‚Р В°Р Р†Р В»Р ВµР Р…Р С‘Р Вµ РЎРѓРЎР‚Р В°Р В·РЎС“ Р Р† Transform, РЎвЂЎРЎвЂљР С•Р В±РЎвЂ№ Р С•Р Р…Р С• Р Р…Р Вµ Р В·Р В°Р Р†Р С‘РЎРѓР ВµР В»Р С• Р С•РЎвЂљ Р Р…Р В°Р В»Р С‘РЎвЂЎР С‘РЎРЏ Animation.
    transform->surfaceForward = anim.surfaceForward;

    speed = std::max(0.01f, speed);
    // Р вЂўРЎРѓР В»Р С‘ caller Р С—Р ВµРЎР‚Р ВµР Т‘Р В°Р В» 0.0f, Р С—Р С•Р Т‘Р С—РЎР‚РЎвЂ№Р С–Р С‘Р Р†Р В°Р Р…Р С‘Р Вµ Р Т‘Р С•Р В»Р В¶Р Р…Р С• Р С—Р С•Р В»Р Р…Р С•РЎРѓРЎвЂљРЎРЉРЎР‹ Р С•РЎвЂљР С”Р В»РЎР‹РЎвЂЎР С‘РЎвЂљРЎРЉРЎРѓРЎРЏ.
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

    refreshBuildPreview();
    return true;
}

void InputController::updateAnimations(float dt) {
    ecs_.update(dt);
    refreshBuildPreview();
}

std::optional<int> InputController::explorerCurrentCell() const {
    for (const auto& entityRef : ecs_.entities()) {
        const ecs::Entity& entity = entityRef.get();
        if (isExplorerEntity(ecs_, entity.id)) {
            return entity.currentCell;
        }
    }
    return std::nullopt;
}

bool InputController::isBuildingPlacementMode() const {
    return placementModel_ == PlacementModel::Factory || placementModel_ == PlacementModel::Mine;
}

bool InputController::canBuildOnCell(int cellId) const {
    return buildPreviewCells_.contains(cellId);
}

void InputController::refreshBuildPreview() {
    const auto explorerCell = explorerCurrentCell();
    const int anchorCell = (explorerCell && *explorerCell >= 0) ? *explorerCell : -1;
    const bool shouldShowPreview = isBuildingPlacementMode();

    QSet<int> nextPreviewCells;
    if (shouldShowPreview && anchorCell >= 0 && anchorCell < scene_.model().cellCount()) {
        const auto& cells = scene_.model().cells();
        const auto& origin = cells[static_cast<size_t>(anchorCell)];
        for (int neighbor : origin.neighbors) {
            if (neighbor < 0) {
                continue;
            }
            if (!isCellOccupied(neighbor)) {
                nextPreviewCells.insert(neighbor);
            }
        }
    }

    if (lastBuildPreviewActive_ == shouldShowPreview &&
        lastBuildPreviewAnchorCell_ == anchorCell &&
        buildPreviewCells_ == nextPreviewCells) {
        return;
    }

    lastBuildPreviewActive_ = shouldShowPreview;
    lastBuildPreviewAnchorCell_ = anchorCell;
    buildPreviewCells_ = std::move(nextPreviewCells);
    uploadSelection();
}


