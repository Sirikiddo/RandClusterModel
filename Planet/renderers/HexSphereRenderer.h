#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLContext>
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
#include "model/ModelHandler.h"

class TerrainRenderer;
class WaterRenderer;
class EntityRenderer;
class OverlayRenderer;

class HexSphereRenderer {
public:

    void setOreAnimationTime(float time);
    void setOreVisualizationEnabled(bool enabled);

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

    void renderScene(const RenderGraph& graph, const RenderCamera& camera, const SceneLighting& lighting);

    bool ready() const { return glReady_; }
    GLuint envCubemap() const { return envCubemap_; }

private:
    GLuint makeProgram(const char* vs, const char* fs);
    void generateEnvCubemap();
    void initPyramidGeometry();
    void withContext(const std::function<void()>& task);
    void setExternalContextActive(bool active) { externalContextActive_ = active; }
    void uploadWireInternal(const std::vector<float>& vertices, GLenum usage);
    void uploadTerrainInternal(const TerrainMesh& mesh, GLenum usage);
    void uploadSelectionOutlineInternal(const std::vector<float>& vertices);
    void uploadPathInternal(const std::vector<QVector3D>& points);
    void uploadWaterInternal(const WaterGeometryData& data);

    QOpenGLWidget* owner_ = nullptr;
    QOpenGLContext* glContext_ = nullptr;
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    PerformanceStats* stats_ = nullptr;

    bool glReady_ = false;
    bool externalContextActive_ = false;

    GLuint envCubemap_ = 0;
    GLint uEnvMap_ = -1;

    GLuint progWire_ = 0, progTerrain_ = 0, progSel_ = 0;
    GLuint progWater_ = 0, progModel_ = 0;
    GLint uMVP_Wire_ = -1, uMVP_Terrain_ = -1, uMVP_Sel_ = -1;
    GLint uModel_ = -1, uLightDir_ = -1;
    GLint uMVP_Water_ = -1, uTime_Water_ = -1, uLightDir_Water_ = -1, uViewPos_Water_ = -1;
    GLint uMVP_Model_ = -1, uModel_Model_ = -1, uLightDir_Model_ = -1, uViewPos_Model_ = -1, uColor_Model_ = -1, uUseTexture_ = -1;

    GLuint vaoWire_ = 0, vboPositions_ = 0;
    GLuint vaoTerrain_ = 0, vboTerrainPos_ = 0, vboTerrainCol_ = 0, vboTerrainNorm_ = 0, iboTerrain_ = 0;
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

    std::shared_ptr<ModelHandler> treeModel_;

    std::unique_ptr<TerrainRenderer> terrainRenderer_;
    std::unique_ptr<WaterRenderer> waterRenderer_;
    std::unique_ptr<EntityRenderer> entityRenderer_;
    std::unique_ptr<OverlayRenderer> overlayRenderer_;

    float oreAnimationTime_ = 0.0f;
    bool oreVisualizationEnabled_ = true;
};
