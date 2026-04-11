#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QVector3D>
#include <QOpenGLWidget>
#include <QtOpenGL>
#include <GL/gl.h>
#include <functional>
#include <memory>
#include <vector>

#include "ui/PerformanceStats.h"
#include "ECS/ComponentStorage.h"
#include "controllers/HexSphereSceneController.h"
#include "resources/HexSphereWidget_shaders.h"
#include "model/ModelHandler.h"
#include "model/CarModelHandler.h"

#include <QMutex>
#include <QtConcurrent/QtConcurrent>

class TerrainRenderer;
class WaterRenderer;
class EntityRenderer;
class OverlayRenderer;
class EngineFacade;

class HexSphereRenderer {
public:

    void setOreAnimationTime(float time);
    void setOreVisualizationEnabled(bool enabled);
    void attachEngine(EngineFacade* engine) { engine_ = engine; }

    void updateVisibility(const QVector3D& cameraPos);


    struct RenderGraph {
        const HexSphereSceneController& scene;
        const ecs::ComponentStorage& ecs;
        float heightStep = 0.0f;
    };

    struct RenderCamera {
        QMatrix4x4 view;
        QMatrix4x4 projection;
    };

    struct SceneLighting {
        QVector3D direction;
        float waterTime = 0.0f;
    };

    struct RenderContext {
        const RenderGraph& graph;
        const RenderCamera& camera;
        const SceneLighting& lighting;
        QMatrix4x4 mvp;
        QVector3D cameraPos;
    };

    struct UploadOptions {
        GLenum terrainUsage = GL_STATIC_DRAW;
        GLenum wireUsage = GL_STATIC_DRAW;
        bool useStaticBuffers = true;
    };

    explicit HexSphereRenderer(QOpenGLWidget* owner);
    ~HexSphereRenderer();

    void initialize(QOpenGLWidget* owner, QOpenGLFunctions_3_3_Core* gl, PerformanceStats* stats);
    void resize(int w, int h, float devicePixelRatio, QMatrix4x4& proj);

    void uploadWire(const std::vector<float>& vertices, GLenum usage);
    void uploadTerrain(const TerrainMesh& mesh, GLenum usage);
    void uploadSelectionOutline(const std::vector<float>& vertices);
    void uploadPath(const std::vector<QVector3D>& points);
    void uploadWater(const WaterGeometryData& data);
    void uploadScene(const HexSphereSceneController& scene, const UploadOptions& options);
    void uploadSceneWithTerrainOverride(
        const HexSphereSceneController& scene,
        const TerrainMesh& terrainMesh,
        const UploadOptions& options);

    void renderScene(const RenderGraph& graph, const RenderCamera& camera, const SceneLighting& lighting);

    bool ready() const { return glReady_; }
    GLuint envCubemap() const { return envCubemap_; }

    // ��� ������� �����������
    struct VisibilityBuffer {
        std::vector<uint32_t> indices;
        bool ready = false;
        int frameAge = 0;
    };

    VisibilityBuffer buffers_[2];
    int currentBuffer_ = 0;
    int nextBuffer_ = 1;

    void swapBuffers() {
        std::swap(currentBuffer_, nextBuffer_);
        buffers_[nextBuffer_].ready = false;
    }

private:
    GLuint makeProgram(const char* vs, const char* fs);
    void generateEnvCubemap();
    void initPyramidGeometry();
    void withContext(const std::function<void()>& task);
    void uploadWireInternal(const std::vector<float>& vertices, GLenum usage);
    void uploadTerrainInternal(const TerrainMesh& mesh, GLenum usage);
    void uploadSelectionOutlineInternal(const std::vector<float>& vertices);
    void uploadPathInternal(const std::vector<QVector3D>& points);
    void uploadWaterInternal(const WaterGeometryData& data);
    void loadContributorModel();
    void renderContributorModel(const RenderContext& ctx);

    // ����� �����
    void recreateTerrainVAO();
    void uploadFullTerrainIndexBuffer();

    HexSphereSceneController* lastScene_ = nullptr;
    EngineFacade* engine_ = nullptr;

    QOpenGLWidget* owner_ = nullptr;
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    PerformanceStats* stats_ = nullptr;

    bool glReady_ = false;

    GLuint envCubemap_ = 0;
    GLint uEnvMap_ = -1;

    GLuint progWire_ = 0, progTerrain_ = 0, progSel_ = 0;
    GLuint progWater_ = 0, progModel_ = 0;
    GLint uMVP_Wire_ = -1, uMVP_Terrain_ = -1, uMVP_Sel_ = -1;
    GLint uModel_ = -1, uLightDir_ = -1;
    GLint uNormalMatrix_ = -1;
    GLint uMVP_Water_ = -1, uTime_Water_ = -1, uLightDir_Water_ = -1, uViewPos_Water_ = -1;
    GLint uMVP_Model_ = -1, uModel_Model_ = -1, uLightDir_Model_ = -1, uViewPos_Model_ = -1, uColor_Model_ = -1, uUseTexture_ = -1;

    GLuint vaoWire_ = 0, vboPositions_ = 0;
    QOpenGLVertexArrayObject vaoTerrain_;
    GLuint vboTerrainPos_ = 0, vboTerrainCol_ = 0, vboTerrainNorm_ = 0, iboTerrain_ = 0;
    GLuint vaoSel_ = 0, vboSel_ = 0;
    GLuint vaoPath_ = 0, vboPath_ = 0;
    GLuint vaoPyramid_ = 0, vboPyramid_ = 0;
    GLuint vaoWater_ = 0, vboWaterPos_ = 0, iboWater_ = 0, vboWaterEdgeFlags_ = 0;

    GLsizei lineVertexCount_ = 0;
    GLsizei terrainIndexCount_ = 0;
    GLsizei selLineVertexCount_ = 0;
    GLsizei pathVertexCount_ = 0;
    GLsizei pyramidVertexCount_ = 0;
    GLsizei waterIndexCount_ = 0;
    bool terrainVisibilityDirty_ = true;

    std::shared_ptr<ModelHandler> treeModel_;
    std::shared_ptr<ModelHandler> firTreeModel_;
    std::shared_ptr<ModelHandler> contributorModel_;
    std::shared_ptr<ModelHandler> contributorWoodModel_;
    std::shared_ptr<ModelHandler> contributorLeavesModel_;

    QVector3D contributorModelPosition_{ 0.0f, 0.0f, 0.0f };
    QVector3D contributorModelRotationDegrees_{ 0.0f, 0.0f, 0.0f };
    QVector3D contributorModelColor_{ 0.24f, 0.62f, 0.22f };
    QVector3D contributorWoodColor_{ 0.46f, 0.27f, 0.12f };
    QVector3D contributorLeavesColor_{ 0.18f, 0.58f, 0.20f };
    float contributorModelScale_ = 0.5f;

    std::unique_ptr<TerrainRenderer> terrainRenderer_;
    std::unique_ptr<WaterRenderer> waterRenderer_;
    std::unique_ptr<EntityRenderer> entityRenderer_;
    std::unique_ptr<OverlayRenderer> overlayRenderer_;

    GLuint treeColorTexture_ = 0;
    GLint uTreeTexture_ = -1;

    GLuint createTreeColorTexture();

    float oreAnimationTime_ = 0.0f;
    bool oreVisualizationEnabled_ = true;
    bool firstRenderDone_ = false;

    size_t totalIndexCount_ = 0;

    std::shared_ptr<CarModelHandler> carModel_;
};
