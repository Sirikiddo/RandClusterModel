#include "renderers/HexSphereRenderer.h"

#include <QOpenGLWidget>
#include <QtDebug>

#include <QOpenGLVertexArrayObject> 
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "contributor/ContributorAsset.h"
#include "core/AppViewConfig.h"
#include "resources/HexSphereWidget_shaders.h"
#include "model/SurfacePlacement.h"
#include "renderers/EntityRenderer.h"
#include "ui/OverlayRenderer.h"
#include "renderers/TerrainRenderer.h"
#include "renderers/WaterRenderer.h"

namespace {
    QMatrix4x4 surfaceBasisFromForward(const QVector3D& unitUp, QVector3D forwardTangent) {
        forwardTangent = forwardTangent - QVector3D::dotProduct(forwardTangent, unitUp) * unitUp;
        if (forwardTangent.length() < 1e-4f) {
            QVector3D refX(1, 0, 0);
            QVector3D right = refX - QVector3D::dotProduct(refX, unitUp) * unitUp;
            if (right.length() < 0.01f) {
                refX = QVector3D(0, 0, 1);
                right = refX - QVector3D::dotProduct(refX, unitUp) * unitUp;
            }
            right.normalize();
            forwardTangent = QVector3D::crossProduct(unitUp, right).normalized();
        }
        else {
            forwardTangent.normalize();
        }

        const QVector3D right = QVector3D::crossProduct(forwardTangent, unitUp).normalized();

        QMatrix4x4 rotation;
        rotation.setColumn(0, QVector4D(right, 0.0f));
        rotation.setColumn(1, QVector4D(unitUp, 0.0f));
        rotation.setColumn(2, QVector4D(forwardTangent, 0.0f));
        return rotation;
    }

    void orientTreeToSurface(QMatrix4x4& matrix, const QVector3D& normal) {
        const QVector3D up = normal.normalized();
        const QVector3D seedForward = (qAbs(QVector3D::dotProduct(up, QVector3D(0, 0, 1))) > 0.99f)
            ? QVector3D(1, 0, 0)
            : QVector3D(0, 0, 1);
        matrix = matrix * surfaceBasisFromForward(up, seedForward);
    }

    uint64_t quantizedHashFloat(float value) {
        const auto quantized = static_cast<int64_t>(std::llround(static_cast<double>(value) * 100000.0));
        return static_cast<uint64_t>(quantized);
    }
}

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
    particleRenderer_.reset();

    if (treeModel_.use_count() == 1 && treeModel_) {
        treeModel_->clearGPUResources();
    }
    if (firTreeModel_.use_count() == 1 && firTreeModel_) {
        firTreeModel_->clearGPUResources();
    }
    if (carModel_.use_count() == 1 && carModel_) {
        carModel_->clearGPUResources();
    }
    if (factoryModel_.use_count() == 1 && factoryModel_) {
        factoryModel_->clearGPUResources();
    }
    if (mineModel_.use_count() == 1 && mineModel_) {
        mineModel_->clearGPUResources();
    }
    if (contributorModel_.use_count() == 1 && contributorModel_) {
        contributorModel_->clearGPUResources();
    }
    if (contributorWoodModel_.use_count() == 1 && contributorWoodModel_) {
        contributorWoodModel_->clearGPUResources();
    }
    if (contributorLeavesModel_.use_count() == 1 && contributorLeavesModel_) {
        contributorLeavesModel_->clearGPUResources();
    }

    if (progWire_)    gl_->glDeleteProgram(progWire_);
    if (progTerrain_) gl_->glDeleteProgram(progTerrain_);
    if (progSel_)     gl_->glDeleteProgram(progSel_);
    if (progWater_)   gl_->glDeleteProgram(progWater_);
    if (progModel_)   gl_->glDeleteProgram(progModel_);
    if (progFactory_) gl_->glDeleteProgram(progFactory_);
    if (progSteam_)   gl_->glDeleteProgram(progSteam_);

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
    progFactory_ = makeProgram(VS_FACTORY, FS_FACTORY);
    progSteam_ = makeProgram(VS_STEAM, FS_STEAM);

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

    gl_->glUseProgram(progFactory_);
    uMVP_Factory_ = gl_->glGetUniformLocation(progFactory_, "uMVP");
    uModel_Factory_ = gl_->glGetUniformLocation(progFactory_, "uModel");
    uLightDir_Factory_ = gl_->glGetUniformLocation(progFactory_, "uLightDir");
    uViewPos_Factory_ = gl_->glGetUniformLocation(progFactory_, "uViewPos");
    uColor_Factory_ = gl_->glGetUniformLocation(progFactory_, "uColor");
    uUseTexture_Factory_ = gl_->glGetUniformLocation(progFactory_, "uUseTexture");

    gl_->glUseProgram(progSteam_);
    uMVP_Steam_ = gl_->glGetUniformLocation(progSteam_, "uMVP");
    uModel_Steam_ = gl_->glGetUniformLocation(progSteam_, "uModel");
    uTime_Steam_ = gl_->glGetUniformLocation(progSteam_, "uTime");
    uViewPos_Steam_ = gl_->glGetUniformLocation(progSteam_, "uViewPos");

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

    particleRenderer_ = std::make_unique<ParticleRenderer>();
    particleRenderer_->initialize();

    {
        ContributorAsset treeAsset = buildContributorAsset();
        planetTreeParticleTemplate_ = treeAsset.particles;
        if (treeAsset.render.scale > 1e-5f) {
            const float inverseTemplateScale = 1.0f / treeAsset.render.scale;
            for (auto& particle : planetTreeParticleTemplate_) {
                particle.restPosition *= inverseTemplateScale;
                particle.position *= inverseTemplateScale;
            }
        }

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
            qDebug() << "Procedural oak tree model loaded";
        }
        else {
            treeModel_ = ModelHandler::loadShared("resources/tree.obj");
            if (treeModel_) {
                treeModel_->uploadToGPU();
                qDebug() << "Fallback oak tree model loaded";
            }
        }

        firTreeModel_ = makeTreeFromMesh(treeAsset.generatedMesh, "planet/procedural_tree_fir");
        if (!firTreeModel_) {
            firTreeModel_ = makeTreeFromMesh(treeAsset.generatedWoodMesh, "planet/procedural_tree_fir");
        }
        if (firTreeModel_) {
            firTreeModel_->uploadToGPU();
            qDebug() << "Procedural fir tree model loaded";
        }
        else {
            firTreeModel_ = ModelHandler::loadShared("resources/fir_tree.obj");
            if (firTreeModel_) {
                firTreeModel_->uploadToGPU();
                qDebug() << "Fallback fir tree model loaded";
            }
        }
    }

    if (defaultAppViewConfig().isContributorMode()) {
        loadContributorModel();
    }

    // Р’ HexSphereRenderer::initialize(), РїРѕСЃР»Рµ РІСЃРµС… РѕСЃС‚Р°Р»СЊРЅС‹С… РёРЅРёС†РёР°Р»РёР·Р°С†РёР№:
    owner_->makeCurrent();  // РЈР±РµР¶РґР°РµРјСЃСЏ, С‡С‚Рѕ РєРѕРЅС‚РµРєСЃС‚ С‚РµРєСѓС‰РёР№

    const QString carPath = "resources/car/scene.obj";
    carModel_ = std::make_shared<CarModelHandler>();
    if (!carModel_->loadFromFile(carPath)) {
        qDebug() << "Failed to load car model from:" << carPath;
    }
    else {
        carModel_->uploadToGPU();  // РўРµРїРµСЂСЊ С‚РµРєСЃС‚СѓСЂС‹ Р·Р°РіСЂСѓР·СЏС‚СЃСЏ СЃ Р°РєС‚РёРІРЅС‹Рј РєРѕРЅС‚РµРєСЃС‚РѕРј
        qDebug() << "Car model loaded successfully";
    }

    const QString factoryPath = "resources/factory/scene.obj";
    factoryModel_ = std::make_shared<FactoryModelHandler>();
    if (!factoryModel_->loadFromFile(factoryPath)) {
        qDebug() << "Failed to load factory model from:" << factoryPath;
    }
    else {
        factoryModel_->uploadToGPU();
        qDebug() << "Factory model loaded successfully";
    }

    const QString minePath = "resources/mine/stylized_gold_mine.obj";
    mineModel_ = std::make_shared<MineModelHandler>();
    if (!mineModel_->loadFromFile(minePath)) {
        qDebug() << "Failed to load mine model from:" << minePath;
    }
    else {
        mineModel_->uploadToGPU();
        qDebug() << "Mine model loaded successfully";
    }

    owner_->doneCurrent();  // РњРѕР¶РЅРѕ СЃРЅСЏС‚СЊ РєРѕРЅС‚РµРєСЃС‚, РµСЃР»Рё РЅСѓР¶РЅРѕ

    // РЎРћР—Р”РђРЃРњ Р Р•РќР”Р•Р Р•Р Р« РџРћРЎР›Р• Р’РЎР•РҐ РРќРР¦РРђР›РР—РђР¦РР™
    terrainRenderer_ = std::make_unique<TerrainRenderer>(
        gl_,
        progTerrain_,
        uMVP_Terrain_,
        uModel_,
        uLightDir_,
        uNormalMatrix_,
        vaoTerrain_.objectId()  // в†ђ objectId() РІРѕР·РІСЂР°С‰Р°РµС‚ GLuint
    );

    waterRenderer_ = std::make_unique<WaterRenderer>(gl_, progWater_, uMVP_Water_, uTime_Water_, uLightDir_Water_, uViewPos_Water_, uEnvMap_, envCubemap_, vaoWater_, waterIndexCount_);
    entityRenderer_ = std::make_unique<EntityRenderer>(
        gl_, progWire_, progSel_, progModel_, progFactory_, progSteam_,
        uMVP_Wire_, uMVP_Sel_, uMVP_Model_, uModel_Model_,
        uLightDir_Model_, uViewPos_Model_, uColor_Model_, uUseTexture_,
        uMVP_Factory_, uModel_Factory_, uLightDir_Factory_, uViewPos_Factory_, uColor_Factory_, uUseTexture_Factory_,
        uMVP_Steam_, uModel_Steam_, uTime_Steam_, uViewPos_Steam_,
        vaoPyramid_, pyramidVertexCount_, treeModel_, firTreeModel_, carModel_, factoryModel_, mineModel_);
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

        if (!contributorWoodModel_ && !contributorLeavesModel_ && !asset.generatedMesh.positions.empty()) {
            contributorModel_ = std::make_shared<ModelHandler>();
            if (!contributorModel_->loadFromMesh("contributor/generated", std::move(asset.generatedMesh))) {
                contributorModel_.reset();
            }
        }
    }

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
    if (!asset.particles.empty() && particleRenderer_) {
        particleRenderer_->updateParticles(asset.particles);
        qDebug() << "Contributor particles loaded:" << asset.particles.size();
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

    terrainIndexCount_ = GLsizei(mesh.idx.size());
    totalIndexCount_ = mesh.idx.size();  // РЎРѕС…СЂР°РЅСЏРµРј РґР»СЏ СЃС‚Р°С‚РёСЃС‚РёРєРё

    // РЎРѕР·РґР°РµРј VAO РѕРґРёРЅ СЂР°Р·
    if (!vaoTerrain_.isCreated()) {
        recreateTerrainVAO();
    }

    qDebug() << "uploadTerrainInternal - total indexCount:" << terrainIndexCount_;
}

// ========== РќРћР’Р«Р™ РњР•РўРћР” Р”Р›РЇ РћР‘РќРћР’Р›Р•РќРРЇ Р’РР”РРњРћРЎРўР ==========
//void HexSphereRenderer::updateVisibility(const QVector3D& cameraPos) {
//    if (!glReady_ || !lastScene_) return;
//
//    // РћР±РЅРѕРІР»СЏРµРј РїРѕР·РёС†РёСЋ РєР°РјРµСЂС‹ РІ СЃС†РµРЅРµ
//    lastScene_->setCameraPosition(cameraPos);
//
//    // РќР°РіР»СЏРґРЅРѕ СѓР±РµРґРёС‚СЊСЃСЏ, С‡С‚Рѕ С‚СЂРµСѓРіРѕР»СЊРЅРёРєРѕРІ СЂРµР°Р»СЊРЅРѕ РјРµРЅСЊС€Рµ
//    static QElapsedTimer timer;
//    if (!timer.isValid()) {
//        timer.start();
//    }
//    if (timer.elapsed() < 100) return;  // РќРµ С‡Р°С‰Рµ С‡РµРј СЂР°Р· РІ 100 РјСЃ
//    timer.restart();
//
//    // РџСЂРѕРІРµСЂСЏРµРј, РґРІРёРіР°Р»Р°СЃСЊ Р»Рё РєР°РјРµСЂР°
//    if (lastScene_->hasCameraMoved()) {
//        // РџРѕР»СѓС‡Р°РµРј С‚РѕР»СЊРєРѕ РІРёРґРёРјС‹Рµ РёРЅРґРµРєСЃС‹
//        std::vector<uint32_t> visibleIndices = lastScene_->getVisibleIndices(cameraPos);
//
//        if (visibleIndices.empty()) {
//            qDebug() << "No visible triangles!";
//            return;
//        }
//
//        qDebug() << "Updating visibility - visible indices:" << visibleIndices.size();
//
//        // РћР±РЅРѕРІР»СЏРµРј РўРћР›Р¬РљРћ РёРЅРґРµРєСЃРЅС‹Р№ Р±СѓС„РµСЂ
//        gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
//        gl_->glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
//            visibleIndices.size() * sizeof(uint32_t),
//            visibleIndices.data());
//
//        // РћР±РЅРѕРІР»СЏРµРј СЃС‡РµС‚С‡РёРє РґР»СЏ РѕС‚СЂРёСЃРѕРІРєРё
//        terrainIndexCount_ = GLsizei(visibleIndices.size());
//
//        // РћС‚РјРµС‡Р°РµРј, С‡С‚Рѕ РєР°РјРµСЂР° РѕР±СЂР°Р±РѕС‚Р°РЅР°
//        lastScene_->updateLastCameraPosition();
//
//        qDebug() << "Visibility updated, drawing" << terrainIndexCount_ << "indices";
//    }
//}

void HexSphereRenderer::updateVisibility(const QVector3D& cameraPos) {
    if (!glReady_ || !lastScene_) return;

    // РћР±РЅРѕРІР»СЏРµРј РїРѕР·РёС†РёСЋ РєР°РјРµСЂС‹ РІ СЃС†РµРЅРµ
    lastScene_->setCameraPosition(cameraPos);
    if (!lastScene_->supportsTerrainVisibility()) {
        terrainIndexCount_ = 0;
        return;
    }

    // РЈР”РђР›РРўР¬ СЌС‚Сѓ СЃС‚СЂРѕРєСѓ:
    // lastScene_->updatePrediction(cameraPos);

    // РСЃРїРѕР»СЊР·СѓРµРј Р°РґР°РїС‚РёРІРЅСѓСЋ Р»РѕРіРёРєСѓ РґР»СЏ РѕРїСЂРµРґРµР»РµРЅРёСЏ РЅРµРѕР±С…РѕРґРёРјРѕСЃС‚Рё РѕР±РЅРѕРІР»РµРЅРёСЏ
    if (lastScene_->shouldUpdateVisibility() && lastScene_->hasCameraMoved(0.1f)) {
        QElapsedTimer filterTimer;
        filterTimer.start();

        // РРЎРџР РђР’РРўР¬: РёСЃРїРѕР»СЊР·РѕРІР°С‚СЊ РѕР±С‹С‡РЅСѓСЋ РІРµСЂСЃРёСЋ, РЅРµ СЃ РїСЂРµРґСЃРєР°Р·Р°РЅРёРµРј
        std::vector<uint32_t> visibleIndices = lastScene_->getVisibleIndices(cameraPos);

        qint64 elapsed = filterTimer.elapsed();

        // РЎС‚Р°С‚РёСЃС‚РёРєР°
        static int updateCount = 0;
        static qint64 totalTime = 0;
        updateCount++;
        totalTime += elapsed;

        if (updateCount % 10 == 0) {
            qDebug() << "=== ADAPTIVE UPDATE STATS ===";  // Р’РµСЂРЅСѓС‚СЊ СЃС‚Р°СЂРѕРµ РЅР°Р·РІР°РЅРёРµ
            qDebug() << "Avg filter time:" << (totalTime / updateCount) << "ms";
            qDebug() << "Triangles:" << (visibleIndices.size() / 3)
                << "/" << (lastScene_->terrain().idx.size() / 3);
            qDebug() << "==============================";
        }

        // РћР±РЅРѕРІР»СЏРµРј РёРЅРґРµРєСЃРЅС‹Р№ Р±СѓС„РµСЂ
        gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
        gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            visibleIndices.size() * sizeof(uint32_t),
            visibleIndices.data(),
            GL_DYNAMIC_DRAW);

        terrainIndexCount_ = GLsizei(visibleIndices.size());
        lastScene_->updateLastCameraPosition();
    }
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
        windField_.direction = QVector3D(0.8f, 0.2f, 0.4f).normalized();
        windField_.strength = 0.35f;
        windField_.gustStrength = 0.4f;
        windField_.gustSpeed = 1.8f;
        windField_.turbulence = 0.2f;
        particleRenderer_->update(0.016f, windField_, contributorModelPosition_);
        particleRenderer_->render(ctx.mvp, ctx.camera.view, ctx.cameraPos);
    }
}

void HexSphereRenderer::uploadScene(const HexSphereSceneController& scene, const UploadOptions& options) {
    qDebug() << "uploadScene called, setting lastScene_";
    lastScene_ = const_cast<HexSphereSceneController*>(&scene);

    withContext([&]() {
        uploadWireInternal(scene.buildWireVertices(), options.wireUsage);
        uploadTerrainInternal(scene.terrain(), options.terrainUsage);
        uploadSelectionOutlineInternal(scene.buildSelectionOutlineVertices());
        uploadPathInternal({});
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

    constexpr size_t kMaxTreesWithParticles = 96;
    constexpr size_t kMaxParticlesTotal = 30000;
    const size_t treeCount = std::min(placements.size(), kMaxTreesWithParticles);

    uint64_t placementHash = 1469598103934665603ull;
    auto hashCombine = [&placementHash](uint64_t value) {
        placementHash ^= value;
        placementHash *= 1099511628211ull;
    };

    hashCombine(static_cast<uint64_t>(treeCount));
    for (size_t i = 0; i < treeCount; ++i) {
        const auto& placement = placements[i];
        const QVector3D treePos = computeSurfacePoint(ctx.graph.scene, placement, ctx.graph.heightStep);
        hashCombine(static_cast<uint64_t>(placement.cellId + 10007));
        hashCombine(static_cast<uint64_t>(placement.treeType));
        hashCombine(static_cast<uint64_t>(placement.triangleIdx + 1009));
        hashCombine(quantizedHashFloat(placement.baryU));
        hashCombine(quantizedHashFloat(placement.baryV));
        hashCombine(quantizedHashFloat(placement.baryW));
        hashCombine(quantizedHashFloat(placement.rotation));
        hashCombine(quantizedHashFloat(placement.scale));
        hashCombine(quantizedHashFloat(treePos.x()));
        hashCombine(quantizedHashFloat(treePos.y()));
        hashCombine(quantizedHashFloat(treePos.z()));
    }

    if (placementHash != planetTreeParticlesPlacementHash_) {
        std::vector<ContributorParticle> worldParticles;
        worldParticles.reserve(std::min(kMaxParticlesTotal, treeCount * planetTreeParticleTemplate_.size()));
        const size_t particlesPerTreeBudget = std::max<size_t>(1, kMaxParticlesTotal / treeCount);
        const size_t sourceStep = std::max<size_t>(1, planetTreeParticleTemplate_.size() / particlesPerTreeBudget);

        for (size_t i = 0; i < treeCount; ++i) {
            const auto& placement = placements[i];
            const QVector3D treePos = computeSurfacePoint(ctx.graph.scene, placement, ctx.graph.heightStep);
            const QVector3D up = treePos.normalized();

            QMatrix4x4 transform;
            transform.translate(treePos);
            orientTreeToSurface(transform, up);
            transform.rotate(placement.rotation * 180.0f / 3.14159f, 0, 1, 0);
            const float baseScale = (placement.treeType == TreeType::Fir) ? 0.045f : 0.04f;
            transform.scale(baseScale * placement.scale);

            QVector3D foliageColor = placement.foliageColor;
            if (placement.colorType == TreePlacement::TreeColorType::Autumn) {
                foliageColor = QVector3D(0.85f, 0.48f, 0.18f);
            }
            else {
                foliageColor = QVector3D(0.22f, 0.68f, 0.24f);
            }

            size_t emittedForTree = 0;
            for (size_t sourceIndex = 0; sourceIndex < planetTreeParticleTemplate_.size(); sourceIndex += sourceStep) {
                if (worldParticles.size() >= kMaxParticlesTotal) {
                    break;
                }
                if (emittedForTree >= particlesPerTreeBudget) {
                    break;
                }
                const auto& source = planetTreeParticleTemplate_[sourceIndex];
                ContributorParticle particle = source;
                particle.restPosition = (transform * QVector4D(source.restPosition, 1.0f)).toVector3D();
                particle.position = (transform * QVector4D(source.position, 1.0f)).toVector3D();
                particle.normal = transform.mapVector(source.normal).normalized();
                particle.color = foliageColor;
                particle.size *= 1.25f;
                particle.velocity = QVector3D(0.0f, 0.0f, 0.0f);
                particle.windWeight = 0.0f;
                worldParticles.push_back(particle);
                ++emittedForTree;
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

    particleRenderer_->render(ctx.mvp, ctx.camera.view, ctx.cameraPos);
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

