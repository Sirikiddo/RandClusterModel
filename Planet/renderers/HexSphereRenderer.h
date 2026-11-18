#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QVector3D>
#include <QSize>
#include <QImage>
#include <QOpenGLWidget>
#include <QtOpenGL>
#include <GL/gl.h>
#include <functional>
#include <cstdint>
#include <memory>
#include <vector>

#include "ui/PerformanceStats.h"
#include "ECS/ComponentStorage.h"
#include "controllers/HexSphereSceneController.h"
#include "resources/HexSphereWidget_shaders.h"
#include "model/ModelHandler.h"
#include "model/CarModelHandler.h"
#include "model/FactoryModelHandler.h"
#include "model/MineModelHandler.h"
#include "renderers/ParticleRenderer.h"
#include "renderers/WaterShaderUniforms.h"
#include "contributor/ContributorParticles.h"
#include "contributor/TreeBuilder.h"

#include <QMutex>
#include <QtConcurrent/QtConcurrent>

class TerrainRenderer;
class WaterRenderer;
class EntityRenderer;
class OverlayRenderer;
class PlanetSurfaceAtlasPass;

class HexSphereRenderer {
public:

    void setOreVisualizationEnabled(bool enabled);
    void setRockDebugMode(int mode) { rockDebugMode_ = mode; }

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
        double waterTime = 0.0;
    };

    struct RenderContext {
        const RenderGraph& graph;
        const RenderCamera& camera;
        const SceneLighting& lighting;
        QMatrix4x4 mvp;
        QMatrix4x4 invViewProjection;
        QVector3D cameraPos;
        QSize viewportSize;
    };

    struct UploadOptions {
        GLenum terrainUsage = GL_STATIC_DRAW;
        bool useStaticBuffers = true;
    };

    explicit HexSphereRenderer(QOpenGLWidget* owner);
    ~HexSphereRenderer();

    void initialize(QOpenGLWidget* owner, QOpenGLFunctions_3_3_Core* gl, PerformanceStats* stats);
    void resize(int w, int h, float devicePixelRatio, QMatrix4x4& proj);

    void uploadTerrain(const TerrainMesh& mesh, GLenum usage);
    void uploadSelectionOutline(const std::vector<float>& vertices);
    void uploadPath(const std::vector<QVector3D>& points);
    void uploadRoad(const std::vector<float>& vertices);
    void uploadScene(const HexSphereSceneController& scene, const UploadOptions& options);
    void uploadTerrainHydrology(const HexSphereSceneController& scene, GLenum terrainUsage);

    void renderScene(const RenderGraph& graph, const RenderCamera& camera, const SceneLighting& lighting);

    bool ready() const { return glReady_; }
    GLuint envCubemap() const { return envCubemap_; }

    // Для двойной буферизации видимости
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

    void updateTerrainOreData(const TerrainMesh& mesh);

private:
    GLuint makeProgram(const QByteArray& vs, const QByteArray& fs,
        const QByteArray& gs = QByteArray{});
    void generateEnvCubemap();
    void ensureSceneDepthTexture(int width, int height);
    void copySceneDepthToTexture(int width, int height);
    void rebuildPlanetSurfaceAtlas(const HexSphereModel& model, uint64_t terrainRevision);
    void initPyramidGeometry();
    void withContext(const std::function<void()>& task);
    void uploadTerrainInternal(const TerrainMesh& mesh, GLenum usage);
    void uploadSelectionOutlineInternal(const std::vector<float>& vertices);
    void uploadPathInternal(const std::vector<QVector3D>& points);
    void uploadRiverInternal(const std::vector<float>& vertices);
    void uploadRiverLineInternal(const std::vector<float>& vertices);
    void loadContributorModel();
    void renderContributorModel(const RenderContext& ctx);
    void renderRockSandbox(const RenderContext& ctx);              // Песочница камня (шаг 2)
    GLuint createTexture2DFromImage(const QImage& image, const char* debugName);
    GLuint createTextureArray(const std::vector<QImage>& images, const char* debugName);
    void ensureRockTextures();
    void renderProceduralTreeParticles(const RenderContext& ctx);  // Твой метод

    void recreateTerrainVAO();

    HexSphereSceneController* lastScene_ = nullptr;

    QOpenGLWidget* owner_ = nullptr;
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    PerformanceStats* stats_ = nullptr;

    bool glReady_ = false;

    GLuint envCubemap_ = 0;
    GLuint sceneDepthTexture_ = 0;
    GLuint sceneDepthFbo_ = 0;
    GLuint planetRadiusAtlas_ = 0;
    GLuint planetSurfaceKindAtlas_ = 0;
    GLuint planetShoreDistanceAtlas_ = 0;
    QSize sceneDepthTextureSize_;
    GLuint progWire_ = 0, progTerrain_ = 0, progSel_ = 0;
    GLuint progWater_ = 0, progModel_ = 0, progFactory_ = 0, progSteam_ = 0;
    GLuint progRock_ = 0;   // Песочница камня (нормал-маппинг + свой свет)
    GLint uMVP_Wire_ = -1, uMVP_Terrain_ = -1, uMVP_Sel_ = -1;
    GLint uModel_ = -1, uLightDir_ = -1;
    GLint uNormalMatrix_ = -1, uOreEnabled_ = -1;
    GLint uRockAlbedo_Terrain_ = -1, uRockNormal_Terrain_ = -1;
    GLint uRockUVScale_Terrain_ = -1, uRockNormalStrength_Terrain_ = -1, uRockStrength_Terrain_ = -1;
    GLint uRockAppearanceSeed_Terrain_ = -1;
    GLint uMVP_Model_ = -1, uModel_Model_ = -1, uLightDir_Model_ = -1, uViewPos_Model_ = -1, uColor_Model_ = -1, uUseTexture_ = -1;
    GLint uMVP_Factory_ = -1, uModel_Factory_ = -1, uLightDir_Factory_ = -1, uViewPos_Factory_ = -1, uColor_Factory_ = -1, uUseTexture_Factory_ = -1;
    GLint uMVP_Steam_ = -1, uModel_Steam_ = -1, uTime_Steam_ = -1, uViewPos_Steam_ = -1;

    // -------- Униформы каменного шейдера (песочница) --------
    GLint uMVP_Rock_ = -1, uModel_Rock_ = -1, uLightDir_Rock_ = -1, uViewPos_Rock_ = -1;
    GLint uAlbedo_Rock_ = -1, uUVTiling_Rock_ = -1, uNoiseScale_Rock_ = -1;
    GLint uNormalStrength_Rock_ = -1, uParallaxScale_Rock_ = -1, uRoughness_Rock_ = -1;
    GLint uNormalFlipY_Rock_ = -1, uDebugMode_Rock_ = -1;
    GLuint rockAlbedoTex_ = 0, rockNormalTex_ = 0, rockRoughTex_ = 0, rockHeightTex_ = 0;
    GLuint rockAlbedoArrayTex_ = 0, rockNormalArrayTex_ = 0;
    bool rockBakedReady_ = false;
    GLint uUseBaked_Rock_ = -1, uAlbedoMap_Rock_ = -1, uNormalMap_Rock_ = -1;
    GLint uRoughMap_Rock_ = -1, uHeightMap_Rock_ = -1, uBakeNormalStrength_Rock_ = -1;
    GLuint mossAlbedoTex_ = 0, mossNormalTex_ = 0;
    GLint uMossAlbedoMap_Rock_ = -1, uMossNormalMap_Rock_ = -1, uMossRoughness_Rock_ = -1;
    GLint uMaskScale_Rock_ = -1, uMaskAmount_Rock_ = -1, uMaskSlope_Rock_ = -1;

    // -------- Униформы мха на планете ----------
    GLint uMossAlbedo_Terrain_ = -1, uMossNormal_Terrain_ = -1;
    GLint uMossAmount_Terrain_ = -1, uMossScale_Terrain_ = -1;
    GLint uMossPatchScale_Terrain_ = -1, uMossCoverage_Terrain_ = -1;
    GLint uMossSlope_Terrain_ = -1, uMossNormalStrength_Terrain_ = -1;


    float rockLightPhase_ = 0.0f;   // фаза облёта источника света вокруг плитки
    int rockDebugMode_ = 0;         // 0 — финал, см. setRockDebugMode()

    QOpenGLVertexArrayObject vaoTerrain_;
    GLuint vboTerrainPos_ = 0, vboTerrainCol_ = 0, vboTerrainNorm_ = 0, vboTerrainOre_ = 0, iboTerrain_ = 0;
    GLuint vboTerrainSurface_ = 0;
    GLuint vaoSel_ = 0, vboSel_ = 0;
    GLuint vaoPath_ = 0, vboPath_ = 0;
    GLuint vaoPyramid_ = 0, vboPyramid_ = 0;
    // Core profile needs a VAO even when vertices come from gl_VertexID.
    GLuint vaoWater_ = 0;

    GLsizei terrainIndexCount_ = 0;
    GLsizei selLineVertexCount_ = 0;
    GLsizei pathVertexCount_ = 0;
    GLsizei pyramidVertexCount_ = 0;

    GLuint vaoRoad_ = 0;
    GLuint vboRoad_ = 0;
    GLsizei roadVertexCount_ = 0;
    GLint uRoadColor_ = -1;
    GLint uIsRoad_ = -1;

    // Реки: та же лента, что и дорога, но рисуется водой (см. resources/terrain).
    GLuint vaoRiver_ = 0;
    GLuint vboRiver_ = 0;
    GLsizei riverVertexCount_ = 0;
    GLuint vaoRiverLine_ = 0;
    GLuint vboRiverLine_ = 0;
    GLsizei riverLineVertexCount_ = 0;
    GLuint progRiverLine_ = 0;
    GLint uMVP_RiverLine_ = -1;
    GLint uViewPos_RiverLine_ = -1;
    GLint uViewportSize_RiverLine_ = -1;
    GLint uWidthPx_RiverLine_ = -1;
    uint64_t uploadedRiverRevision_ = 0;
    GLint uIsRiver_ = -1;
    GLint uRiverWaveAmplitude_ = -1;
    GLint uRiverWaveNormalAmplitude_ = -1;
    GLint uRiverWaterDepth_ = -1;
    GLint uRiverDepthTolerance_ = -1;
    GLint uSceneDepthTex_Terrain_ = -1;
    // Общие с морем uniform-ы: волны, оптика, окружение (см. WaterShaderUniforms.h).
    WaterShaderLocations waterLocationsTerrain_;

    // ===== ТОЛЬКО ПРОЦЕДУРНЫЕ ДЕРЕВЬЯ (БЕЗ OBJ) =====
    std::shared_ptr<ModelHandler> proceduralLeavesModel_;

    // Модели для Contributor-режима
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
    std::unique_ptr<PlanetSurfaceAtlasPass> surfaceAtlasPass_;

    bool oreVisualizationEnabled_ = true;
    bool firstRenderDone_ = false;

    size_t totalIndexCount_ = 0;

    std::shared_ptr<CarModelHandler> carModel_;
    std::shared_ptr<FactoryModelHandler> factoryModel_;
    std::shared_ptr<MineModelHandler> mineModel_;

    // Рендерер частиц для крон процедурных деревьев
    std::unique_ptr<ParticleRenderer> particleRenderer_;
    uint64_t treeParticlesPlacementHash_ = 0;
    ContributorWindField windField_;

    // 7 видов × 8 вариантов моделей
    std::vector<std::vector<std::shared_ptr<ModelHandler>>> proceduralTreeModelsBySpecies;

    // 7 видов × 8 вариантов шаблонов частиц
    std::vector<std::vector<std::vector<ContributorParticle>>> treeParticlesBySpecies;

    // 7 видов × 8 вариантов (цвета + параметры + масштаб)
    std::vector<std::vector<TreeBuilder::TreeVariant>> speciesVariants;};
