#include "renderers/HexSphereRenderer.h"

#include <QOpenGLWidget>
#include <QtDebug>

#include <QOpenGLVertexArrayObject>

#include "contributor/ContributorAsset.h"
#include "core/AppViewConfig.h"
#include "resources/HexSphereWidget_shaders.h"
#include "model/SurfacePlacement.h"
#include "renderers/EntityRenderer.h"
#include "ui/OverlayRenderer.h"
#include "renderers/TerrainRenderer.h"
#include "renderers/WaterRenderer.h"
#include "dag/EngineFacade.h"
#include <algorithm>

HexSphereRenderer::HexSphereRenderer(QOpenGLWidget* owner)
    : owner_(owner) {
}

HexSphereRenderer::~HexSphereRenderer() {
    if (!glReady_ || !owner_ || !gl_) {
        return;
    }

    // РџСЂРѕРІРµСЂСЏРµРј, СЃСѓС‰РµСЃС‚РІСѓРµС‚ Р»Рё РµС‰С‘ РєРѕРЅС‚РµРєСЃС‚ OpenGL
    if (!QOpenGLContext::currentContext()) {
        // РљРѕРЅС‚РµРєСЃС‚ СѓР¶Рµ СѓРЅРёС‡С‚РѕР¶РµРЅ - РЅРёС‡РµРіРѕ РЅРµ РґРµР»Р°РµРј
        glReady_ = false;
        return;
    }

    owner_->makeCurrent();

    // 1. РЎРЅР°С‡Р°Р»Р° СѓРґР°Р»СЏРµРј СЂРµРЅРґРµСЂРµСЂС‹ (РѕРЅРё РёСЃРїРѕР»СЊР·СѓСЋС‚ OpenGL)
    terrainRenderer_.reset();
    waterRenderer_.reset();
    entityRenderer_.reset();
    overlayRenderer_.reset();

    // 2. РћС‡РёС‰Р°РµРј РјРѕРґРµР»СЊ РґРµСЂРµРІСЊРµРІ
    if (treeModel_.use_count() == 1 && treeModel_) {
        treeModel_->clearGPUResources();
    }

    // 3. РЈРґР°Р»СЏРµРј С€РµР№РґРµСЂРЅС‹Рµ РїСЂРѕРіСЂР°РјРјС‹
    if (progWire_)    gl_->glDeleteProgram(progWire_);
    if (progTerrain_) gl_->glDeleteProgram(progTerrain_);
    if (progSel_)     gl_->glDeleteProgram(progSel_);
    if (progWater_)   gl_->glDeleteProgram(progWater_);
    if (progModel_)   gl_->glDeleteProgram(progModel_);

    // 4. РЈРґР°Р»СЏРµРј VAO (РєСЂРѕРјРµ vaoTerrain_ - РѕРЅ СѓРґР°Р»РёС‚СЃСЏ Р°РІС‚РѕРјР°С‚РёС‡РµСЃРєРё)
    if (vaoWire_ != 0)     gl_->glDeleteVertexArrays(1, &vaoWire_);
    if (vaoSel_ != 0)      gl_->glDeleteVertexArrays(1, &vaoSel_);
    if (vaoWater_ != 0)    gl_->glDeleteVertexArrays(1, &vaoWater_);
    if (vaoPyramid_ != 0)  gl_->glDeleteVertexArrays(1, &vaoPyramid_);

    // 5. РЇРІРЅРѕ СѓРЅРёС‡С‚РѕР¶Р°РµРј QOpenGLVertexArrayObject
    if (vaoTerrain_.isCreated()) {
        // РЈР±РµР¶РґР°РµРјСЃСЏ, С‡С‚Рѕ VAO РЅРµ РїСЂРёРІСЏР·Р°РЅ
        gl_->glBindVertexArray(0);
        vaoTerrain_.destroy();
    }

    // 6. РЈРґР°Р»СЏРµРј Р±СѓС„РµСЂС‹
    if (vboPositions_)   gl_->glDeleteBuffers(1, &vboPositions_);
    if (vboTerrainPos_)  gl_->glDeleteBuffers(1, &vboTerrainPos_);
    if (vboTerrainCol_)  gl_->glDeleteBuffers(1, &vboTerrainCol_);
    if (vboTerrainNorm_) gl_->glDeleteBuffers(1, &vboTerrainNorm_);
    if (iboTerrain_)     gl_->glDeleteBuffers(1, &iboTerrain_);
    if (vboSel_)         gl_->glDeleteBuffers(1, &vboSel_);
    if (vboPath_)        gl_->glDeleteBuffers(1, &vboPath_);
    if (vboPyramid_)     gl_->glDeleteBuffers(1, &vboPyramid_);
    if (vboWaterPos_)    gl_->glDeleteBuffers(1, &vboWaterPos_);
    if (iboWater_)       gl_->glDeleteBuffers(1, &iboWater_);
    if (vboWaterEdgeFlags_) gl_->glDeleteBuffers(1, &vboWaterEdgeFlags_);

    if (QOpenGLContext::currentContext()) {
        owner_->doneCurrent();
    }

    glReady_ = false;
}

GLuint HexSphereRenderer::makeProgram(const char* vs, const char* fs) {
    GLuint v = gl_->glCreateShader(GL_VERTEX_SHADER);
    gl_->glShaderSource(v, 1, &vs, nullptr);
    gl_->glCompileShader(v);

    GLint success;
    gl_->glGetShaderiv(v, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        gl_->glGetShaderInfoLog(v, 512, nullptr, infoLog);
        qDebug() << "Vertex shader compilation failed:" << infoLog;
    }

    GLuint f = gl_->glCreateShader(GL_FRAGMENT_SHADER);
    gl_->glShaderSource(f, 1, &fs, nullptr);
    gl_->glCompileShader(f);

    gl_->glGetShaderiv(f, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        gl_->glGetShaderInfoLog(f, 512, nullptr, infoLog);
        qDebug() << "Fragment shader compilation failed:" << infoLog;
    }

    GLuint p = gl_->glCreateProgram();
    gl_->glAttachShader(p, v);
    gl_->glAttachShader(p, f);
    gl_->glLinkProgram(p);

    gl_->glGetProgramiv(p, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        gl_->glGetProgramInfoLog(p, 512, nullptr, infoLog);
        qDebug() << "Shader program linking failed:" << infoLog;
    }

    gl_->glDeleteShader(v);
    gl_->glDeleteShader(f);
    return p;
}

void HexSphereRenderer::initialize(QOpenGLWidget* owner, QOpenGLFunctions_3_3_Core* gl, PerformanceStats* stats) {
    owner_ = owner;
    gl_ = gl;
    stats_ = stats;
    glReady_ = true;

    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glEnable(GL_CULL_FACE);
    gl_->glCullFace(GL_BACK);
    gl_->glFrontFace(GL_CCW);

    progWire_ = makeProgram(VS_WIRE, FS_WIRE);
    progTerrain_ = makeProgram(VS_TERRAIN, FS_TERRAIN);
    progSel_ = makeProgram(VS_WIRE, FS_SEL);
    progWater_ = makeProgram(VS_WATER, FS_WATER);
    progModel_ = makeProgram(VS_MODEL, FS_MODEL);

    gl_->glUseProgram(progWire_);
    uMVP_Wire_ = gl_->glGetUniformLocation(progWire_, "uMVP");

    gl_->glUseProgram(progTerrain_);
    uMVP_Terrain_ = gl_->glGetUniformLocation(progTerrain_, "uMVP");
    uModel_ = gl_->glGetUniformLocation(progTerrain_, "uModel");
    uLightDir_ = gl_->glGetUniformLocation(progTerrain_, "uLightDir");
    uNormalMatrix_ = gl_->glGetUniformLocation(progTerrain_, "uNormalMatrix");

    gl_->glUseProgram(progSel_);
    uMVP_Sel_ = gl_->glGetUniformLocation(progSel_, "uMVP");

    gl_->glUseProgram(progWater_);
    uMVP_Water_ = gl_->glGetUniformLocation(progWater_, "uMVP");
    uTime_Water_ = gl_->glGetUniformLocation(progWater_, "uTime");
    uLightDir_Water_ = gl_->glGetUniformLocation(progWater_, "uLightDir");
    uViewPos_Water_ = gl_->glGetUniformLocation(progWater_, "uViewPos");
    uEnvMap_ = gl_->glGetUniformLocation(progWater_, "uEnvMap");
    generateEnvCubemap();

    gl_->glUseProgram(progModel_);
    uMVP_Model_ = gl_->glGetUniformLocation(progModel_, "uMVP");
    uModel_Model_ = gl_->glGetUniformLocation(progModel_, "uModel");
    uLightDir_Model_ = gl_->glGetUniformLocation(progModel_, "uLightDir");
    uViewPos_Model_ = gl_->glGetUniformLocation(progModel_, "uViewPos");
    uColor_Model_ = gl_->glGetUniformLocation(progModel_, "uColor");
    uUseTexture_ = gl_->glGetUniformLocation(progModel_, "uUseTexture");

    gl_->glUseProgram(0);

    gl_->glGenBuffers(1, &vboPositions_);
    gl_->glGenVertexArrays(1, &vaoWire_);
    gl_->glGenBuffers(1, &vboTerrainPos_);
    gl_->glGenBuffers(1, &vboTerrainCol_);
    gl_->glGenBuffers(1, &vboTerrainNorm_);
    gl_->glGenBuffers(1, &iboTerrain_);
    gl_->glGenVertexArrays(1, &vaoSel_);
    gl_->glGenBuffers(1, &vboSel_);
    gl_->glGenVertexArrays(1, &vaoPath_);
    gl_->glGenBuffers(1, &vboPath_);
    gl_->glGenBuffers(1, &vboWaterPos_);
    gl_->glGenBuffers(1, &iboWater_);
    gl_->glGenBuffers(1, &vboWaterEdgeFlags_);
    gl_->glGenVertexArrays(1, &vaoWater_);

    gl_->glBindVertexArray(vaoWire_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPositions_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);

    gl_->glBindVertexArray(vaoSel_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboSel_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);

    gl_->glBindVertexArray(vaoPath_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPath_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);

    gl_->glBindVertexArray(vaoWater_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterEdgeFlags_);
    gl_->glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(1);
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    gl_->glBindVertexArray(0);

    initPyramidGeometry();

    // ========== 1. РЎРќРђР§РђР›Рђ РЎРћР—Р”РђРЃРњ Р Р•РќР”Р•Р Р•Р Р« ==========
    terrainRenderer_ = std::make_unique<TerrainRenderer>(
        gl_,
        progTerrain_,
        uMVP_Terrain_,
        uModel_,
        uLightDir_,
        uNormalMatrix_,
        vaoTerrain_.objectId()
    );

    waterRenderer_ = std::make_unique<WaterRenderer>(gl_, progWater_, uMVP_Water_, uTime_Water_, uLightDir_Water_, uViewPos_Water_, uEnvMap_, envCubemap_, vaoWater_, waterIndexCount_);

    // ========== 2. РџРћРўРћРњ РЎРћР—Р”РђРЃРњ ParticleRenderer ==========
    particleRenderer_ = std::make_unique<ParticleRenderer>();
    particleRenderer_->initialize();

    // ========== 3. Р—РђР“Р РЈР–РђР•Рњ ПРОЦЕДУРНУЮ МОДЕЛЬ ДЕРЕВА ==========
    // Reuse the same procedural source used in contributor mode.
    {
        ContributorAsset treeAsset = buildContributorAsset();
        planetTreeParticleTemplate_ = treeAsset.particles;

        auto makeTreeFromMesh = [](const simple3d::Mesh& sourceMesh, const QString& debugName) -> std::shared_ptr<ModelHandler> {
            if (sourceMesh.positions.empty() || sourceMesh.indices.empty()) {
                return nullptr;
            }
            auto model = std::make_shared<ModelHandler>();
            simple3d::Mesh meshCopy = sourceMesh;
            if (!model->loadFromMesh(debugName, std::move(meshCopy))) {
                return nullptr;
            }
            return model;
            };

        treeModel_ = makeTreeFromMesh(treeAsset.generatedMesh, "planet/procedural_tree_oak");
        if (!treeModel_) {
            treeModel_ = makeTreeFromMesh(treeAsset.generatedWoodMesh, "planet/procedural_tree_oak");
        }
        if (treeModel_) {
            treeModel_->uploadToGPU();
            qDebug() << "Procedural oak tree model loaded for planet mode";
        }

        firTreeModel_ = makeTreeFromMesh(treeAsset.generatedMesh, "planet/procedural_tree_fir");
        if (!firTreeModel_) {
            firTreeModel_ = makeTreeFromMesh(treeAsset.generatedWoodMesh, "planet/procedural_tree_fir");
        }
        if (firTreeModel_) {
            firTreeModel_->uploadToGPU();
            qDebug() << "Procedural fir tree model loaded for planet mode";
        }
    }

    // ========== 4. РўР•РџР•Р Р¬ РњРћР–РќРћ Р—РђР“Р РЈР–РђРўР¬ CONTRIBUTOR РњРћР”Р•Р›Р¬ (РєРѕС‚РѕСЂР°СЏ РёСЃРїРѕР»СЊР·СѓРµС‚ particleRenderer_) ==========
    if (defaultAppViewConfig().isContributorMode()) {
        loadContributorModel();
    }

    // ========== 5. Р—РђР“Р РЈР–РђР•Рњ РњРђРЁРРќРЈ ==========
    owner_->makeCurrent();

    const QString carPath = "resources/car/scene.obj";
    carModel_ = std::make_shared<CarModelHandler>();
    if (!carModel_->loadFromFile(carPath)) {
        qDebug() << "Failed to load car model from:" << carPath;
    }
    else {
        carModel_->uploadToGPU();
        qDebug() << "Car model loaded successfully";
    }

    owner_->doneCurrent();

    // ========== 6. РћРЎРўРђР›Р¬РќР«Р• Р Р•РќР”Р•Р Р•Р Р« ==========
    entityRenderer_ = std::make_unique<EntityRenderer>(
        gl_, progWire_, progSel_, progModel_,
        uMVP_Wire_, uMVP_Sel_, uMVP_Model_, uModel_Model_,
        uLightDir_Model_, uViewPos_Model_, uColor_Model_, uUseTexture_,
        vaoPyramid_, pyramidVertexCount_, treeModel_, firTreeModel_, carModel_);
    overlayRenderer_ = std::make_unique<OverlayRenderer>(gl_, progWire_, progSel_, uMVP_Wire_, uMVP_Sel_, vaoWire_, vaoSel_, vaoPath_, lineVertexCount_, selLineVertexCount_, pathVertexCount_);

    glReady_ = true;
}

void HexSphereRenderer::loadContributorModel() {
    ContributorAsset asset = buildContributorAsset();
    contributorModelPosition_ = asset.render.position;
    contributorModelRotationDegrees_ = asset.render.rotationDegrees;
    contributorModelColor_ = asset.render.fallbackColor;
    contributorWoodColor_ = asset.woodColor;
    contributorLeavesColor_ = asset.leavesColor;
    contributorModelScale_ = asset.render.scale;

    if (asset.source == ContributorAssetSource::ModelFile) {
        contributorModel_ = ModelHandler::loadShared(asset.modelPath);
    }
    else {
        // Всегда пытаемся загрузить раздельные модели, если есть wood mesh
        if (!asset.generatedWoodMesh.positions.empty()) {
            contributorWoodModel_ = std::make_shared<ModelHandler>();
            if (!contributorWoodModel_->loadFromMesh("contributor/generated_wood", std::move(asset.generatedWoodMesh))) {
                contributorWoodModel_.reset();
            }
        }

        if (!asset.generatedLeavesMesh.positions.empty()) {
            contributorLeavesModel_ = std::make_shared<ModelHandler>();
            if (!contributorLeavesModel_->loadFromMesh("contributor/generated_leaves", std::move(asset.generatedLeavesMesh))) {
                contributorLeavesModel_.reset();
            }
        }

        // Создаём объединённую модель ТОЛЬКО если нет раздельных
        if (!contributorWoodModel_ && !contributorLeavesModel_ && !asset.generatedMesh.positions.empty()) {
            contributorModel_ = std::make_shared<ModelHandler>();
            if (!contributorModel_->loadFromMesh("contributor/generated", std::move(asset.generatedMesh))) {
                contributorModel_.reset();
            }
        }
    }

    // Загружаем модели в GPU
    if (contributorModel_) {
        contributorModel_->uploadToGPU();
        qDebug() << "Contributor model loaded:" << contributorModel_->loadedPath();
    }
    if (contributorWoodModel_) {
        contributorWoodModel_->uploadToGPU();
        qDebug() << "Contributor wood model loaded:" << contributorWoodModel_->loadedPath();
    }
    if (contributorLeavesModel_) {
        contributorLeavesModel_->uploadToGPU();
        qDebug() << "Contributor leaves model loaded:" << contributorLeavesModel_->loadedPath();
    }

    if (!contributorModel_ && !contributorWoodModel_ && !contributorLeavesModel_) {
        qDebug() << "Contributor model failed to load";
    }

    // Загружаем частицы
    if (!asset.particles.empty() && particleRenderer_) {
        particleRenderer_->updateParticles(asset.particles);
        qDebug() << "Loaded" << asset.particles.size() << "particles into renderer";
    }
}
void HexSphereRenderer::resize(int w, int h, float devicePixelRatio, QMatrix4x4& proj) {
    const int pw = int(w * devicePixelRatio);
    const int ph = int(h * devicePixelRatio);
    proj.setToIdentity();
    proj.perspective(50.0f, float(pw) / float(std::max(ph, 1)), 0.01f, 50.0f);
}

void HexSphereRenderer::withContext(const std::function<void()>& task) {
    if (!glReady_) return;

    owner_->makeCurrent();
    task();
    owner_->doneCurrent();
}

void HexSphereRenderer::uploadWireInternal(const std::vector<float>& vertices, GLenum usage) {
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPositions_);
    gl_->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(float)), vertices.data(), usage);
    lineVertexCount_ = GLsizei(vertices.size() / 3);
    if (stats_) {
        stats_->updateMemoryStats(lineVertexCount_, 0, 0);
    }
}

void HexSphereRenderer::uploadTerrainInternal(const TerrainMesh& mesh, GLenum usage) {
    qDebug() << "uploadTerrainInternal - original indices:" << mesh.idx.size();

    // Р—Р°РіСЂСѓР¶Р°РµРј РІРµСЂС€РёРЅС‹ (СЌС‚Рѕ РЅРµ РјРµРЅСЏРµС‚СЃСЏ)
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER, mesh.pos.size() * sizeof(float), mesh.pos.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glBufferData(GL_ARRAY_BUFFER, mesh.col.size() * sizeof(float), mesh.col.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glBufferData(GL_ARRAY_BUFFER, mesh.norm.size() * sizeof(float), mesh.norm.data(), usage);

    // РќР• Р¤РР›Р¬РўР РЈР•Рњ Р·РґРµСЃСЊ - СЃРѕС…СЂР°РЅСЏРµРј РІСЃРµ РёРЅРґРµРєСЃС‹
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        mesh.idx.size() * sizeof(uint32_t),
        mesh.idx.data(),
        GL_DYNAMIC_DRAW);  // Р’СЃРµРіРґР° DYNAMIC, С‚Р°Рє РєР°Рє Р±СѓРґРµРј РјРµРЅСЏС‚СЊ

    fullTerrainMesh_ = mesh;
    fullTerrainIndices_ = mesh.idx;
    terrainVisibility_.reset();
    terrainIndexCount_ = GLsizei(mesh.idx.size());
    terrainVisibilityDirty_ = true;

    // РЎРѕР·РґР°РµРј VAO РѕРґРёРЅ СЂР°Р·
    if (!vaoTerrain_.isCreated()) {
        recreateTerrainVAO();
    }

    qDebug() << "uploadTerrainInternal - total indexCount:" << terrainIndexCount_;
}


void HexSphereRenderer::updateVisibility(const QVector3D& cameraPos) {
    if (!glReady_ || fullTerrainIndices_.empty()) return;


    // РСЃРїРѕР»СЊР·СѓРµРј Р°РґР°РїС‚РёРІРЅСѓСЋ Р»РѕРіРёРєСѓ РґР»СЏ РѕРїСЂРµРґРµР»РµРЅРёСЏ РЅРµРѕР±С…РѕРґРёРјРѕСЃС‚Рё РѕР±РЅРѕРІР»РµРЅРёСЏ
    const bool updateForCamera = terrainVisibility_.shouldUpdate(cameraPos);
    if (terrainVisibilityDirty_ || updateForCamera) {
        QElapsedTimer filterTimer;
        filterTimer.start();

        std::vector<uint32_t> visibleIndices;
        if (engine_ && engine_->prepareVisibleTerrainIndices(cameraPos)) {
            if (const auto* dagVisibleIndices = engine_->currentVisibleTerrainIndices()) {
                visibleIndices = *dagVisibleIndices;
            }
        }
        if (visibleIndices.empty()) {
            visibleIndices = buildVisibleTerrainIndices(fullTerrainMesh_, cameraPos);
        }
        if (visibleIndices.empty()) {
            uploadFullTerrainIndexBuffer();
            terrainVisibility_.markVisibilityApplied(cameraPos);
            terrainVisibilityDirty_ = false;
            return;
        }

        qint64 elapsed = filterTimer.elapsed();

        // РЎС‚Р°С‚РёСЃС‚РёРєР°
        static int updateCount = 0;
        static qint64 totalTime = 0;
        updateCount++;
        totalTime += elapsed;

        if (updateCount % 10 == 0) {
            qDebug() << "=== ADAPTIVE UPDATE STATS ===";
            qDebug() << "Avg filter time:" << (totalTime / updateCount) << "ms";
            qDebug() << "Triangles:" << (visibleIndices.size() / 3)
                << "/" << (fullTerrainIndices_.size() / 3);
            qDebug() << "==============================";
        }

        // РћР±РЅРѕРІР»СЏРµРј РёРЅРґРµРєСЃРЅС‹Р№ Р±СѓС„РµСЂ
        gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
        gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            visibleIndices.size() * sizeof(uint32_t),
            visibleIndices.data(),
            GL_DYNAMIC_DRAW);

        terrainIndexCount_ = GLsizei(visibleIndices.size());
        terrainVisibility_.markVisibilityApplied(cameraPos);
        terrainVisibilityDirty_ = false;
    }
}

void HexSphereRenderer::uploadFullTerrainIndexBuffer() {
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        fullTerrainIndices_.size() * sizeof(uint32_t),
        fullTerrainIndices_.empty() ? nullptr : fullTerrainIndices_.data(),
        GL_DYNAMIC_DRAW);

    terrainIndexCount_ = GLsizei(fullTerrainIndices_.size());
}

void HexSphereRenderer::recreateTerrainVAO() {
    if (!glReady_ || !gl_) {
        qDebug() << "OpenGL not ready!";
        return;
    }

    // Р•СЃР»Рё VAO СѓР¶Рµ СЃРѕР·РґР°РЅ, РЅРµ СЃРѕР·РґР°РµРј Р·Р°РЅРѕРІРѕ
    if (vaoTerrain_.isCreated()) {
        qDebug() << "VAO already created, skipping recreation";
        return;
    }

    qDebug() << "Creating terrain VAO - START";

    // РЎРѕР·РґР°РµРј РЅРѕРІС‹Р№ VAO
    if (!vaoTerrain_.create()) {
        qDebug() << "Failed to create VAO!";
        return;
    }

    vaoTerrain_.bind();

    // РќР°СЃС‚СЂР°РёРІР°РµРј Р°С‚СЂРёР±СѓС‚С‹
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(1);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(2);

    // РџСЂРёРІСЏР·С‹РІР°РµРј РёРЅРґРµРєСЃРЅС‹Р№ Р±СѓС„РµСЂ
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);

    vaoTerrain_.release();

    // РћР±РЅРѕРІР»СЏРµРј VAO РІ СЂРµРЅРґРµСЂРµСЂРµ
    if (terrainRenderer_) {
        terrainRenderer_->updateVAO(vaoTerrain_.objectId());
    }

    qDebug() << "Creating terrain VAO - END, ID:" << vaoTerrain_.objectId();
}

void HexSphereRenderer::uploadSelectionOutlineInternal(const std::vector<float>& vertices) {
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboSel_);
    gl_->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
    selLineVertexCount_ = GLsizei(vertices.size() / 3);
}

void HexSphereRenderer::uploadPathInternal(const std::vector<QVector3D>& points) {
    std::vector<float> buffer;
    buffer.reserve(points.size() * 3);
    for (const auto& p : points) {
        buffer.push_back(p.x());
        buffer.push_back(p.y());
        buffer.push_back(p.z());
    }
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPath_);
    gl_->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(buffer.size() * sizeof(float)), buffer.empty() ? nullptr : buffer.data(), GL_DYNAMIC_DRAW);
    pathVertexCount_ = GLsizei(buffer.size() / 3);
}

void HexSphereRenderer::uploadWaterInternal(const WaterGeometryData& data) {
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER, data.positions.size() * sizeof(float), data.positions.empty() ? nullptr : data.positions.data(), GL_STATIC_DRAW);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterEdgeFlags_);
    gl_->glBufferData(GL_ARRAY_BUFFER, data.edgeFlags.size() * sizeof(float), data.edgeFlags.empty() ? nullptr : data.edgeFlags.data(), GL_STATIC_DRAW);

    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(uint32_t), data.indices.empty() ? nullptr : data.indices.data(), GL_STATIC_DRAW);

    gl_->glBindVertexArray(vaoWater_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterEdgeFlags_);
    gl_->glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(1);
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    gl_->glBindVertexArray(0);

    waterIndexCount_ = static_cast<GLsizei>(data.indices.size());
}

void HexSphereRenderer::uploadWire(const std::vector<float>& vertices, GLenum usage) {
    withContext([&]() { uploadWireInternal(vertices, usage); });
}

void HexSphereRenderer::uploadTerrain(const TerrainMesh& mesh, GLenum usage) {
    withContext([&]() { uploadTerrainInternal(mesh, usage); });
}

void HexSphereRenderer::uploadSelectionOutline(const std::vector<float>& vertices) {
    withContext([&]() { uploadSelectionOutlineInternal(vertices); });
}

void HexSphereRenderer::uploadPath(const std::vector<QVector3D>& points) {
    withContext([&]() { uploadPathInternal(points); });
}

void HexSphereRenderer::uploadWater(const WaterGeometryData& data) {
    withContext([&]() { uploadWaterInternal(data); });
}

void HexSphereRenderer::renderContributorModel(const RenderContext& ctx) {
    const bool hasSplitModel =
        (contributorWoodModel_ && contributorWoodModel_->isInitialized()) ||
        (contributorLeavesModel_ && contributorLeavesModel_->isInitialized());
    const bool hasSingleModel = contributorModel_ && contributorModel_->isInitialized();

    // Отладка
    qDebug() << "hasSplitModel:" << hasSplitModel;
    qDebug() << "woodModel exists:" << (contributorWoodModel_ && contributorWoodModel_->isInitialized());
    qDebug() << "leavesModel exists:" << (contributorLeavesModel_ && contributorLeavesModel_->isInitialized());
    qDebug() << "woodColor:" << contributorWoodColor_;
    qDebug() << "leavesColor:" << contributorLeavesColor_;

    if (!hasSplitModel && !hasSingleModel) {
        return;
    }

    const GLboolean cullWasEnabled = gl_->glIsEnabled(GL_CULL_FACE);
    gl_->glDisable(GL_CULL_FACE);

    QMatrix4x4 model;
    model.translate(contributorModelPosition_);
    model.rotate(contributorModelRotationDegrees_.x(), 1.0f, 0.0f, 0.0f);
    model.rotate(contributorModelRotationDegrees_.y(), 0.0f, 1.0f, 0.0f);
    model.rotate(contributorModelRotationDegrees_.z(), 0.0f, 0.0f, 1.0f);
    model.scale(contributorModelScale_);

    gl_->glUseProgram(progModel_);
    const GLint uIsCar = gl_->glGetUniformLocation(progModel_, "uIsCar");
    if (uIsCar >= 0) {
        gl_->glUniform1i(uIsCar, 0);
    }
    const GLint uUseFoliageColor = gl_->glGetUniformLocation(progModel_, "uUseFoliageColor");
    if (uUseFoliageColor >= 0) {
        gl_->glUniform1i(uUseFoliageColor, 0);
    }

    const QMatrix4x4 mvp = ctx.camera.projection * ctx.camera.view * model;
    if (hasSplitModel) {
        if (contributorWoodModel_) {
            if (uUseFoliageColor >= 0) {
                gl_->glUniform1i(uUseFoliageColor, 0);
            }
            const GLint uTrunkColor = gl_->glGetUniformLocation(progModel_, "uTrunkColor");
            if (uTrunkColor >= 0) {
                gl_->glUniform3f(uTrunkColor, contributorWoodColor_.x(), contributorWoodColor_.y(), contributorWoodColor_.z());
            }
            contributorWoodModel_->draw(progModel_, mvp, model, ctx.camera.view, contributorWoodColor_, /*forceTextureOff=*/true);
        }
        if (contributorLeavesModel_) {
            if (uUseFoliageColor >= 0) {
                gl_->glUniform1i(uUseFoliageColor, 1);
            }
            const GLint uFoliageColor = gl_->glGetUniformLocation(progModel_, "uFoliageColor");
            if (uFoliageColor >= 0) {
                gl_->glUniform3f(uFoliageColor, contributorLeavesColor_.x(), contributorLeavesColor_.y(), contributorLeavesColor_.z());
            }
            contributorLeavesModel_->draw(progModel_, mvp, model, ctx.camera.view, contributorLeavesColor_, /*forceTextureOff=*/true);
        }
    }
    else {
        if (uUseFoliageColor >= 0) {
            gl_->glUniform1i(uUseFoliageColor, 0);
        }
        const GLint uTrunkColor = gl_->glGetUniformLocation(progModel_, "uTrunkColor");
        if (uTrunkColor >= 0) {
            gl_->glUniform3f(uTrunkColor, contributorModelColor_.x(), contributorModelColor_.y(), contributorModelColor_.z());
        }
        contributorModel_->draw(
            progModel_,
            mvp,
            model,
            ctx.camera.view,
            contributorModelColor_,
            /*forceTextureOff=*/false);
    }

    if (cullWasEnabled) {
        gl_->glEnable(GL_CULL_FACE);
    }
    else {
        gl_->glDisable(GL_CULL_FACE);
    }
    if (particleRenderer_ && particleRenderer_->isInitialized()) {
        particleRenderer_->update(0.016f, windField_, contributorModelPosition_);
        particleRenderer_->render(ctx.mvp, ctx.camera.view, ctx.cameraPos);
    }
    windTime_ += 0.016f;
    windTime_ += 0.016f;
    // РќР°СЃС‚СЂРѕР№РєР° РІРµС‚СЂР° (РјРѕР¶РЅРѕ Р±СѓРґРµС‚ РјРµРЅСЏС‚СЊ РёР· UI РїРѕР·Р¶Рµ)
    windField_.direction = QVector3D(0.8f, 0.2f, 0.4f).normalized();
    windField_.strength = 0.35f;
    windField_.gustStrength = 0.4f;
    windField_.gustSpeed = 1.8f;
    windField_.turbulence = 0.2f;

    if (particleRenderer_ && particleRenderer_->isInitialized()) {
        particleRenderer_->update(0.016f, windField_, contributorModelPosition_);
        particleRenderer_->render(ctx.mvp, ctx.camera.view, ctx.cameraPos);
    }
}

void HexSphereRenderer::uploadScene(const HexSphereSceneController& scene, const UploadOptions& options) {
    qDebug() << "uploadScene called";
    const TerrainMesh* terrainMesh = nullptr;
    if (engine_) {
        terrainMesh = engine_->currentTerrainMesh();
    }

    withContext([&]() {
        uploadWireInternal(scene.buildWireVertices(), options.wireUsage);
        uploadTerrainInternal(terrainMesh ? *terrainMesh : scene.terrain(), options.terrainUsage);
        uploadSelectionOutlineInternal(scene.buildSelectionOutlineVertices());
        if (auto path = scene.buildPathPolyline()) {
            uploadPathInternal(*path);
        }
        else {
            uploadPathInternal({});
        }
        uploadWaterInternal(scene.buildWaterGeometry());
        });
    qDebug() << "Buffer strategy:" << (options.useStaticBuffers ? "STATIC" : "DYNAMIC")
        << "(terrain" << options.terrainUsage << ", wire" << options.wireUsage << ")";
}

void HexSphereRenderer::renderScene(const RenderGraph& graph, const RenderCamera& camera, const SceneLighting& lighting) {
    if (!glReady_) return;

    QVector3D cameraPos = (camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

    // РџСЂРѕРІРµСЂСЏРµРј, С‡С‚Рѕ СЂРµРЅРґРµСЂРµСЂС‹ СЃСѓС‰РµСЃС‚РІСѓСЋС‚
    if (!terrainRenderer_ || !waterRenderer_ || !entityRenderer_ || !overlayRenderer_) {
        qDebug() << "ERROR: One or more renderers are null!";
        return;
    }

    // РћР±РЅРѕРІР»СЏРµРј РІРёРґРёРјРѕСЃС‚СЊ
    updateVisibility(cameraPos);

    const float dpr = owner_->devicePixelRatioF();
    gl_->glViewport(0, 0, int(owner_->width() * dpr), int(owner_->height() * dpr));
    gl_->glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // === BASELINE GL STATE ===
    gl_->glDisable(GL_BLEND);
    gl_->glDepthMask(GL_TRUE);
    gl_->glEnable(GL_DEPTH_TEST);

    gl_->glEnable(GL_CULL_FACE);
    gl_->glCullFace(GL_BACK);
    gl_->glFrontFace(GL_CCW);

    gl_->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    gl_->glBindVertexArray(0);
    gl_->glUseProgram(0);

    if (stats_) stats_->startGPUTimer();


    RenderContext ctx{ graph, camera, lighting, camera.projection * camera.view, cameraPos };

    terrainRenderer_->render(ctx, terrainIndexCount_);
    waterRenderer_->render(ctx);
    entityRenderer_->renderEntities(ctx);
    overlayRenderer_->render(ctx);

    // overlay пїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅ/пїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅпїЅ пїЅ пїЅпїЅпїЅпїЅпїЅ пїЅпїЅпїЅпїЅпїЅпїЅ state
    //gl_->glDisable(GL_BLEND);
    //gl_->glDepthMask(GL_TRUE);
    //gl_->glEnable(GL_DEPTH_TEST);
    //gl_->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    //gl_->glBindVertexArray(0);
    //gl_->glUseProgram(0);

    if (graph.scene.isContributorMode()) {
        renderContributorModel(ctx);
    }
    else {
        entityRenderer_->renderTrees(ctx);
        renderPlanetTreeParticles(ctx);
    }

    if (stats_) stats_->stopGPUTimer();
}

void HexSphereRenderer::generateEnvCubemap() {
    if (envCubemap_) return;

    gl_->glGenTextures(1, &envCubemap_);
    gl_->glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap_);

    const int size = 512;
    for (unsigned int i = 0; i < 6; ++i) {
        std::vector<unsigned char> data(size * size * 3);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                int idx = (y * size + x) * 3;
                data[idx] = 100 + (y * 155 / size);
                data[idx + 1] = 150 + (y * 105 / size);
                data[idx + 2] = 255;
            }
        }
        gl_->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    }

    gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

void HexSphereRenderer::initPyramidGeometry() {
    std::vector<float> pyramidVerts = {
        -0.5f, 0.0f, -0.5f,
         0.5f, 0.0f, -0.5f,
         0.5f, 0.0f,  0.5f,
        -0.5f, 0.0f,  0.5f,
        0.0f, 1.0f, 0.0f
    };

    std::vector<uint32_t> pyramidIndices = {
        0, 1, 2,
        0, 2, 3,
        0, 1, 4,
        1, 2, 4,
        2, 3, 4,
        3, 0, 4
    };

    std::vector<float> pyramidVertices;
    pyramidVertices.reserve(pyramidIndices.size() * 3);
    for (uint32_t idx : pyramidIndices) {
        pyramidVertices.push_back(pyramidVerts[idx * 3]);
        pyramidVertices.push_back(pyramidVerts[idx * 3 + 1]);
        pyramidVertices.push_back(pyramidVerts[idx * 3 + 2]);
    }

    gl_->glGenVertexArrays(1, &vaoPyramid_);
    gl_->glGenBuffers(1, &vboPyramid_);
    gl_->glBindVertexArray(vaoPyramid_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPyramid_);
    gl_->glBufferData(GL_ARRAY_BUFFER, pyramidVertices.size() * sizeof(float), pyramidVertices.data(), GL_STATIC_DRAW);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);

    pyramidVertexCount_ = static_cast<GLsizei>(pyramidVertices.size() / 3);
}

void HexSphereRenderer::setOreAnimationTime(float time) {
    oreAnimationTime_ = time;
}

void HexSphereRenderer::setOreVisualizationEnabled(bool enabled) {
    oreVisualizationEnabled_ = enabled;
}

void HexSphereRenderer::renderPlanetTreeParticles(const RenderContext& ctx) {
    if (!particleRenderer_ || !particleRenderer_->isInitialized()) {
        return;
    }
    if (planetTreeParticleTemplate_.empty()) {
        return;
    }

    const auto& placements = ctx.graph.scene.getTreePlacements();
    if (placements.empty()) {
        return;
    }

    // Keep particle count bounded to avoid excessive per-frame CPU/GPU uploads.
    constexpr size_t kMaxTreesWithParticles = 24;
    constexpr size_t kMaxParticlesTotal = 18000;

    const size_t treeCount = std::min(placements.size(), kMaxTreesWithParticles);

    // Rebuild particle cloud only when tree placements change.
    uint64_t placementHash = 1469598103934665603ull;
    auto hashCombine = [&placementHash](uint64_t v) {
        placementHash ^= v;
        placementHash *= 1099511628211ull;
        };
    hashCombine(static_cast<uint64_t>(treeCount));
    for (size_t i = 0; i < treeCount; ++i) {
        const auto& p = placements[i];
        hashCombine(static_cast<uint64_t>(p.cellId + 10007));
        hashCombine(static_cast<uint64_t>(p.treeType));
        hashCombine(static_cast<uint64_t>(p.triangleIdx + 1009));
    }

    if (placementHash != planetTreeParticlesPlacementHash_) {
        std::vector<ContributorParticle> worldParticles;
        worldParticles.reserve(std::min(kMaxParticlesTotal, treeCount * planetTreeParticleTemplate_.size()));

        for (size_t i = 0; i < treeCount; ++i) {
            const auto& placement = placements[i];
            QVector3D treePos = computeSurfacePoint(ctx.graph.scene, placement, ctx.graph.heightStep);
            QVector3D up = treePos.normalized();

            QMatrix4x4 transform;
            transform.translate(treePos);
            transform.rotate(placement.rotation * 180.0f / 3.14159f, up);
            float baseScale = (placement.treeType == TreeType::Fir) ? 0.045f : 0.04f;
            transform.scale(baseScale * placement.scale);

            // Biome-dependent foliage palette for particles.
            QVector3D foliageColor = placement.foliageColor;
            if (placement.colorType == TreePlacement::TreeColorType::Autumn) {
                foliageColor = QVector3D(0.85f, 0.48f, 0.18f);
            }
            else {
                foliageColor = QVector3D(0.22f, 0.68f, 0.24f);
            }

            for (const auto& src : planetTreeParticleTemplate_) {
                if (worldParticles.size() >= kMaxParticlesTotal) {
                    break;
                }
                ContributorParticle p = src;
                const QVector3D localRest = src.restPosition;
                const QVector3D localPos = src.position;
                p.restPosition = (transform * QVector4D(localRest, 1.0f)).toVector3D();
                p.position = (transform * QVector4D(localPos, 1.0f)).toVector3D();
                p.color = foliageColor;
                p.size *= 0.65f;
                p.windWeight *= 1.1f;
                worldParticles.push_back(p);
            }
            if (worldParticles.size() >= kMaxParticlesTotal) {
                break;
            }
        }

        if (worldParticles.empty()) {
            return;
        }

        particleRenderer_->updateParticles(worldParticles);
        planetTreeParticlesPlacementHash_ = placementHash;
    }

    windField_.direction = QVector3D(0.8f, 0.2f, 0.4f).normalized();
    windField_.strength = 0.35f;
    windField_.gustStrength = 0.4f;
    windField_.gustSpeed = 1.8f;
    windField_.turbulence = 0.2f;

    particleRenderer_->update(0.016f, windField_, QVector3D(0.0f, 0.0f, 0.0f));
    particleRenderer_->render(ctx.mvp, ctx.camera.view, ctx.cameraPos);
}
