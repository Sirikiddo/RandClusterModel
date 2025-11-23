#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QVector3D>
#include <memory>
#include <vector>

#include "PerformanceStats.h"
#include "SceneEntity.h"
#include "scene/SceneGraph.h"
#include "HexSphereSceneController.h"
#include "ModelHandler.h"

class QOpenGLWidget;

class HexSphereRenderer {
public:
    explicit HexSphereRenderer(QOpenGLWidget* owner);
    ~HexSphereRenderer();

    void initialize(PerformanceStats& stats);
    void resize(int w, int h, float devicePixelRatio, QMatrix4x4& proj);

    void uploadWire(const std::vector<float>& vertices, GLenum usage);
    void uploadTerrain(const TerrainMesh& mesh, GLenum usage);
    void uploadSelectionOutline(const std::vector<float>& vertices);
    void uploadPath(const std::vector<QVector3D>& points);
    void uploadWater(const WaterGeometryData& data);

    void render(const QMatrix4x4& view, const QMatrix4x4& proj, const HexSphereSceneController& scene,
                const scene::SceneGraph& sceneGraph, float waterTime, const QVector3D& lightDir,
                int selectedEntityId, float heightStep);

    bool ready() const { return glReady_; }
    GLuint envCubemap() const { return envCubemap_; }

private:
    GLuint makeProgram(const char* vs, const char* fs);
    void generateEnvCubemap();
    void initPyramidGeometry();

    QOpenGLWidget* owner_ = nullptr;
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    PerformanceStats* stats_ = nullptr;

    bool glReady_ = false;
    bool useStaticBuffers_ = true;
    GLenum terrainBufferUsage_ = GL_STATIC_DRAW;
    GLenum wireBufferUsage_ = GL_STATIC_DRAW;

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
};
