#include "renderers/HexSphereRenderer.h"

#include <QOpenGLWidget>
#include <QtDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

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
#include "renderers/PlanetSurfaceAtlasPass.h"
#include "ui/OverlayRenderer.h"
#include "renderers/TerrainRenderer.h"
#include "renderers/WaterRenderer.h"

namespace {
    QString shaderIncludePath(const QString& line) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith("#include \"")) return {};
        const int first = trimmed.indexOf('"');
        const int second = trimmed.indexOf('"', first + 1);
        return first >= 0 && second > first ? trimmed.mid(first + 1, second - first - 1) : QString{};
    }

    QByteArray loadShaderSourceRecursive(const QString& path, QSet<QString>& stack) {
        const QString filePath = QFileInfo(path).filePath();
        if (stack.contains(filePath)) {
            qWarning() << "Recursive shader include:" << filePath;
            return {};
        }
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to open shader source:" << filePath;
            return {};
        }
        stack.insert(filePath);
        QByteArray source;
        QTextStream stream(&file);
        const QString baseDir = QFileInfo(filePath).path();
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            const QString includePath = shaderIncludePath(line);
            if (!includePath.isEmpty()) {
                source += loadShaderSourceRecursive(QDir(baseDir).filePath(includePath), stack);
            }
            else {
                source += line.toUtf8();
                source += '\n';
            }
        }
        stack.remove(filePath);
        return source;
    }

    QByteArray loadShaderSource(const QString& path) {
        QSet<QString> stack;
        return loadShaderSourceRecursive(path, stack);
    }

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

    if (!QOpenGLContext::currentContext()) {
        glReady_ = false;
        return;
    }

    owner_->makeCurrent();

    terrainRenderer_.reset();
    waterRenderer_.reset();
    entityRenderer_.reset();
    overlayRenderer_.reset();
    particleRenderer_.reset();
    if (surfaceAtlasPass_) {
        surfaceAtlasPass_->release();
        surfaceAtlasPass_.reset();
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

    if (vaoWire_ != 0)     gl_->glDeleteVertexArrays(1, &vaoWire_);
    if (vaoSel_ != 0)      gl_->glDeleteVertexArrays(1, &vaoSel_);
    if (vaoWater_ != 0)    gl_->glDeleteVertexArrays(1, &vaoWater_);
    if (vaoPyramid_ != 0)  gl_->glDeleteVertexArrays(1, &vaoPyramid_);

    if (vaoTerrain_.isCreated()) {
        gl_->glBindVertexArray(0);
        vaoTerrain_.destroy();
    }

    if (vboPositions_)   gl_->glDeleteBuffers(1, &vboPositions_);
    if (vboTerrainPos_)  gl_->glDeleteBuffers(1, &vboTerrainPos_);
    if (vboTerrainCol_)  gl_->glDeleteBuffers(1, &vboTerrainCol_);
    if (vboTerrainNorm_) gl_->glDeleteBuffers(1, &vboTerrainNorm_);
    if (vboTerrainOre_)  gl_->glDeleteBuffers(1, &vboTerrainOre_);
    if (iboTerrain_)     gl_->glDeleteBuffers(1, &iboTerrain_);
    if (vboSel_)         gl_->glDeleteBuffers(1, &vboSel_);
    if (vboPath_)        gl_->glDeleteBuffers(1, &vboPath_);
    if (vboPyramid_)     gl_->glDeleteBuffers(1, &vboPyramid_);
    if (vboWaterPos_)    gl_->glDeleteBuffers(1, &vboWaterPos_);
    if (iboWater_)       gl_->glDeleteBuffers(1, &iboWater_);
    if (envCubemap_) gl_->glDeleteTextures(1, &envCubemap_);
    if (sceneDepthTexture_) gl_->glDeleteTextures(1, &sceneDepthTexture_);
    if (sceneDepthFbo_) gl_->glDeleteFramebuffers(1, &sceneDepthFbo_);
    if (vaoRoad_ != 0) gl_->glDeleteVertexArrays(1, &vaoRoad_);
    if (vboRoad_ != 0) gl_->glDeleteBuffers(1, &vboRoad_);

    if (QOpenGLContext::currentContext()) {
        owner_->doneCurrent();
    }

    glReady_ = false;
}

GLuint HexSphereRenderer::makeProgram(const QByteArray& vertexSource, const QByteArray& fragmentSource) {
    auto compile = [&](GLenum stage, const QByteArray& source) -> GLuint {
        if (source.isEmpty()) {
            qCritical() << "Cannot compile empty shader source for stage" << stage;
            return 0;
        }
        const char* text = source.constData();
        const GLuint shader = gl_->glCreateShader(stage);
        gl_->glShaderSource(shader, 1, &text, nullptr);
        gl_->glCompileShader(shader);
        GLint success = GL_FALSE;
        gl_->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success == GL_TRUE) return shader;

        GLint logLength = 0;
        gl_->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log(std::max(logLength, 1), '\0');
        gl_->glGetShaderInfoLog(shader, log.size(), nullptr, log.data());
        qCritical().noquote() << "Shader compilation failed:" << log;
        gl_->glDeleteShader(shader);
        return 0;
        };

    const GLuint v = compile(GL_VERTEX_SHADER, vertexSource);
    const GLuint f = compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (v == 0 || f == 0) {
        if (v != 0) gl_->glDeleteShader(v);
        if (f != 0) gl_->glDeleteShader(f);
        return 0;
    }

    GLuint p = gl_->glCreateProgram();
    gl_->glAttachShader(p, v);
    gl_->glAttachShader(p, f);
    gl_->glLinkProgram(p);

    GLint success = GL_FALSE;
    gl_->glGetProgramiv(p, GL_LINK_STATUS, &success);

    gl_->glDeleteShader(v);
    gl_->glDeleteShader(f);
    if (success != GL_TRUE) {
        GLint logLength = 0;
        gl_->glGetProgramiv(p, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log(std::max(logLength, 1), '\0');
        gl_->glGetProgramInfoLog(p, log.size(), nullptr, log.data());
        qCritical().noquote() << "Shader program linking failed:" << log;
        gl_->glDeleteProgram(p);
        return 0;
    }
    return p;
}

void HexSphereRenderer::initialize(QOpenGLWidget* owner,
    QOpenGLFunctions_3_3_Core* gl,
    PerformanceStats* stats) {
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
    progWater_ = makeProgram(
        loadShaderSource(":/Planet/resources/water/water_shell.vert"),
        loadShaderSource(":/Planet/resources/water/water_shell.frag"));
    progModel_ = makeProgram(VS_MODEL, FS_MODEL);
    progFactory_ = makeProgram(VS_FACTORY, FS_FACTORY);
    progSteam_ = makeProgram(VS_STEAM, FS_STEAM);

    if (progWire_ == 0 || progTerrain_ == 0 || progSel_ == 0 || progWater_ == 0
        || progModel_ == 0 || progFactory_ == 0 || progSteam_ == 0) {
        qCritical() << "HexSphereRenderer initialization stopped because a shader program is invalid";
        for (GLuint* program : {
                &progWire_, &progTerrain_, &progSel_, &progWater_,
                &progModel_, &progFactory_, &progSteam_ }) {
            if (*program != 0) gl_->glDeleteProgram(*program);
            *program = 0;
        }
        glReady_ = false;
        return;
    }

    gl_->glUseProgram(progWire_);
    uMVP_Wire_ = gl_->glGetUniformLocation(progWire_, "uMVP");

    gl_->glUseProgram(progTerrain_);
    uMVP_Terrain_ = gl_->glGetUniformLocation(progTerrain_, "uMVP");
    uModel_ = gl_->glGetUniformLocation(progTerrain_, "uModel");
    uLightDir_ = gl_->glGetUniformLocation(progTerrain_, "uLightDir");
    uNormalMatrix_ = gl_->glGetUniformLocation(progTerrain_, "uNormalMatrix");
    uOreEnabled_ = gl_->glGetUniformLocation(progTerrain_, "uOreVisualizationEnabled");
    uRoadColor_ = gl_->glGetUniformLocation(progTerrain_, "uRoadColor");
    uIsRoad_ = gl_->glGetUniformLocation(progTerrain_, "uIsRoad");

    gl_->glUseProgram(progSel_);
    uMVP_Sel_ = gl_->glGetUniformLocation(progSel_, "uMVP");

    generateEnvCubemap();

    surfaceAtlasPass_ = std::make_unique<PlanetSurfaceAtlasPass>();
    surfaceAtlasPass_->initialize(gl_);
    if (!surfaceAtlasPass_->ready()) {
        qCritical() << "HexSphereRenderer initialization stopped because the planet surface atlas is unavailable";
        surfaceAtlasPass_->release();
        surfaceAtlasPass_.reset();
        if (envCubemap_ != 0) { gl_->glDeleteTextures(1, &envCubemap_); envCubemap_ = 0; }
        for (GLuint* program : {
                &progWire_, &progTerrain_, &progSel_, &progWater_,
                &progModel_, &progFactory_, &progSteam_ }) {
            if (*program != 0) gl_->glDeleteProgram(*program);
            *program = 0;
        }
        glReady_ = false;
        return;
    }
    planetRadiusAtlas_ = surfaceAtlasPass_->radiusTexture();
    planetSurfaceKindAtlas_ = surfaceAtlasPass_->kindTexture();
    planetShoreDistanceAtlas_ = surfaceAtlasPass_->shoreDistanceTexture();

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
    gl_->glGenBuffers(1, &vboTerrainOre_);
    gl_->glGenBuffers(1, &iboTerrain_);
    gl_->glGenVertexArrays(1, &vaoSel_);
    gl_->glGenBuffers(1, &vboSel_);
    gl_->glGenVertexArrays(1, &vaoPath_);
    gl_->glGenBuffers(1, &vboPath_);
    gl_->glGenBuffers(1, &vboWaterPos_);
    gl_->glGenBuffers(1, &iboWater_);
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

    gl_->glGenVertexArrays(1, &vaoRoad_);
    gl_->glGenBuffers(1, &vboRoad_);
    gl_->glBindVertexArray(vaoRoad_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboRoad_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);

    gl_->glBindVertexArray(vaoWater_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    gl_->glBindVertexArray(0);

    initPyramidGeometry();

    particleRenderer_ = std::make_unique<ParticleRenderer>();
    particleRenderer_->initialize();

    // ===================================================================
    // ================ ЗАГРУЗКА КЭША ПРОЦЕДУРНЫХ ДЕРЕВЬЕВ ================
    // ===================================================================
    {
        ContributorAsset treeAsset = buildContributorAsset();

        // ---- 1. Модели стволов: 7 видов × N вариантов ----
        auto loadMeshAsModel = [](const simple3d::Mesh& sourceMesh,
            const QString& debugName) -> std::shared_ptr<ModelHandler> {
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

        proceduralTreeModelsBySpecies.clear();
        proceduralTreeModelsBySpecies.resize(treeAsset.speciesMeshes.size());

        for (size_t s = 0; s < treeAsset.speciesMeshes.size(); ++s) {
            proceduralTreeModelsBySpecies[s].resize(treeAsset.speciesMeshes[s].size());
            for (size_t v = 0; v < treeAsset.speciesMeshes[s].size(); ++v) {
                const QString name = QString("tree_s%1_v%2").arg(s).arg(v);
                auto model = loadMeshAsModel(treeAsset.speciesMeshes[s][v], name);
                if (model) {
                    model->uploadToGPU();
                }
                proceduralTreeModelsBySpecies[s][v] = model;
            }
            qDebug() << "Loaded species" << s
                << "variants:" << treeAsset.speciesMeshes[s].size();
        }

        // ---- 2. Шаблоны частиц: 7 видов × N вариантов ----
        treeParticlesBySpecies = std::move(treeAsset.speciesParticles);

        // ---- 3. Метаданные вариантов (цвета/масштаб/параметры) ----
        speciesVariants = std::move(treeAsset.speciesVariants);

        qDebug() << "Loaded tree particle templates:"
            << treeParticlesBySpecies.size() << "species";
    }
    // ===================================================================

    if (defaultAppViewConfig().isContributorMode()) {
        loadContributorModel();
    }

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

    terrainRenderer_ = std::make_unique<TerrainRenderer>(
        gl_, progTerrain_, uMVP_Terrain_, uModel_, uLightDir_,
        uNormalMatrix_, uOreEnabled_, vaoTerrain_.objectId());

    waterRenderer_ = std::make_unique<WaterRenderer>(gl_, progWater_);

    entityRenderer_ = std::make_unique<EntityRenderer>(
        gl_, progWire_, progSel_, progModel_, progFactory_, progSteam_,
        uMVP_Wire_, uMVP_Sel_, uMVP_Model_, uModel_Model_,
        uLightDir_Model_, uViewPos_Model_, uColor_Model_, uUseTexture_,
        uMVP_Factory_, uModel_Factory_, uLightDir_Factory_,
        uViewPos_Factory_, uColor_Factory_, uUseTexture_Factory_,
        uMVP_Steam_, uModel_Steam_, uTime_Steam_, uViewPos_Steam_,
        vaoPyramid_, pyramidVertexCount_, carModel_, factoryModel_, mineModel_);

    overlayRenderer_ = std::make_unique<OverlayRenderer>(
        gl_, progWire_, progSel_, uMVP_Wire_, uMVP_Sel_,
        vaoWire_, vaoSel_, vaoPath_,
        lineVertexCount_, selLineVertexCount_, pathVertexCount_);

    owner_->doneCurrent();
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

    // Р вЂ”Р В°Р С–РЎР‚РЎС“Р В¶Р В°Р ВµР С Р Р†Р ВµРЎР‚РЎв‚¬Р С‘Р Р…РЎвЂ№ (РЎРЊРЎвЂљР С• Р Р…Р Вµ Р СР ВµР Р…РЎРЏР ВµРЎвЂљРЎРѓРЎРЏ)
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER, mesh.pos.size() * sizeof(float), mesh.pos.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glBufferData(GL_ARRAY_BUFFER, mesh.col.size() * sizeof(float), mesh.col.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glBufferData(GL_ARRAY_BUFFER, mesh.norm.size() * sizeof(float), mesh.norm.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainOre_);
    gl_->glBufferData(GL_ARRAY_BUFFER, mesh.ore.size() * sizeof(float), mesh.ore.data(), usage);

    // Р СњР вЂў Р В¤Р ВР вЂєР В¬Р СћР В Р Р€Р вЂўР Сљ Р В·Р Т‘Р ВµРЎРѓРЎРЉ - РЎРѓР С•РЎвЂ¦РЎР‚Р В°Р Р…РЎРЏР ВµР С Р Р†РЎРѓР Вµ Р С‘Р Р…Р Т‘Р ВµР С”РЎРѓРЎвЂ№
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        mesh.idx.size() * sizeof(uint32_t),
        mesh.idx.data(),
        GL_DYNAMIC_DRAW);  // Р вЂ™РЎРѓР ВµР С–Р Т‘Р В° DYNAMIC, РЎвЂљР В°Р С” Р С”Р В°Р С” Р В±РЎС“Р Т‘Р ВµР С Р СР ВµР Р…РЎРЏРЎвЂљРЎРЉ

    terrainIndexCount_ = GLsizei(mesh.idx.size());
    totalIndexCount_ = mesh.idx.size();  // Р РЋР С•РЎвЂ¦РЎР‚Р В°Р Р…РЎРЏР ВµР С Р Т‘Р В»РЎРЏ РЎРѓРЎвЂљР В°РЎвЂљР С‘РЎРѓРЎвЂљР С‘Р С”Р С‘

    // Р РЋР С•Р В·Р Т‘Р В°Р ВµР С VAO Р С•Р Т‘Р С‘Р Р… РЎР‚Р В°Р В·
    if (!vaoTerrain_.isCreated()) {
        recreateTerrainVAO();
    }

    qDebug() << "uploadTerrainInternal - total indexCount:" << terrainIndexCount_;
}

// ========== Р СњР С›Р вЂ™Р В«Р в„ў Р СљР вЂўР СћР С›Р вЂќ Р вЂќР вЂєР Р‡ Р С›Р вЂР СњР С›Р вЂ™Р вЂєР вЂўР СњР ВР Р‡ Р вЂ™Р ВР вЂќР ВР СљР С›Р РЋР СћР В ==========
//void HexSphereRenderer::updateVisibility(const QVector3D& cameraPos) {
//    if (!glReady_ || !lastScene_) return;
//
//    // Р С›Р В±Р Р…Р С•Р Р†Р В»РЎРЏР ВµР С Р С—Р С•Р В·Р С‘РЎвЂ Р С‘РЎР‹ Р С”Р В°Р СР ВµРЎР‚РЎвЂ№ Р Р† РЎРѓРЎвЂ Р ВµР Р…Р Вµ
//    lastScene_->setCameraPosition(cameraPos);
//
//    // Р СњР В°Р С–Р В»РЎРЏР Т‘Р Р…Р С• РЎС“Р В±Р ВµР Т‘Р С‘РЎвЂљРЎРЉРЎРѓРЎРЏ, РЎвЂЎРЎвЂљР С• РЎвЂљРЎР‚Р ВµРЎС“Р С–Р С•Р В»РЎРЉР Р…Р С‘Р С”Р С•Р Р† РЎР‚Р ВµР В°Р В»РЎРЉР Р…Р С• Р СР ВµР Р…РЎРЉРЎв‚¬Р Вµ
//    static QElapsedTimer timer;
//    if (!timer.isValid()) {
//        timer.start();
//    }
//    if (timer.elapsed() < 100) return;  // Р СњР Вµ РЎвЂЎР В°РЎвЂ°Р Вµ РЎвЂЎР ВµР С РЎР‚Р В°Р В· Р Р† 100 Р СРЎРѓ
//    timer.restart();
//
//    // Р СџРЎР‚Р С•Р Р†Р ВµРЎР‚РЎРЏР ВµР С, Р Т‘Р Р†Р С‘Р С–Р В°Р В»Р В°РЎРѓРЎРЉ Р В»Р С‘ Р С”Р В°Р СР ВµРЎР‚Р В°
//    if (lastScene_->hasCameraMoved()) {
//        // Р СџР С•Р В»РЎС“РЎвЂЎР В°Р ВµР С РЎвЂљР С•Р В»РЎРЉР С”Р С• Р Р†Р С‘Р Т‘Р С‘Р СРЎвЂ№Р Вµ Р С‘Р Р…Р Т‘Р ВµР С”РЎРѓРЎвЂ№
//        std::vector<uint32_t> visibleIndices = lastScene_->getVisibleIndices(cameraPos);
//
//        if (visibleIndices.empty()) {
//            qDebug() << "No visible triangles!";
//            return;
//        }
//
//        qDebug() << "Updating visibility - visible indices:" << visibleIndices.size();
//
//        // Р С›Р В±Р Р…Р С•Р Р†Р В»РЎРЏР ВµР С Р СћР С›Р вЂєР В¬Р С™Р С› Р С‘Р Р…Р Т‘Р ВµР С”РЎРѓР Р…РЎвЂ№Р в„– Р В±РЎС“РЎвЂћР ВµРЎР‚
//        gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
//        gl_->glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
//            visibleIndices.size() * sizeof(uint32_t),
//            visibleIndices.data());
//
//        // Р С›Р В±Р Р…Р С•Р Р†Р В»РЎРЏР ВµР С РЎРѓРЎвЂЎР ВµРЎвЂљРЎвЂЎР С‘Р С” Р Т‘Р В»РЎРЏ Р С•РЎвЂљРЎР‚Р С‘РЎРѓР С•Р Р†Р С”Р С‘
//        terrainIndexCount_ = GLsizei(visibleIndices.size());
//
//        // Р С›РЎвЂљР СР ВµРЎвЂЎР В°Р ВµР С, РЎвЂЎРЎвЂљР С• Р С”Р В°Р СР ВµРЎР‚Р В° Р С•Р В±РЎР‚Р В°Р В±Р С•РЎвЂљР В°Р Р…Р В°
//        lastScene_->updateLastCameraPosition();
//
//        qDebug() << "Visibility updated, drawing" << terrainIndexCount_ << "indices";
//    }
//}

void HexSphereRenderer::updateVisibility(const QVector3D& cameraPos) {
    if (!glReady_ || !lastScene_) return;

    // Р С›Р В±Р Р…Р С•Р Р†Р В»РЎРЏР ВµР С Р С—Р С•Р В·Р С‘РЎвЂ Р С‘РЎР‹ Р С”Р В°Р СР ВµРЎР‚РЎвЂ№ Р Р† РЎРѓРЎвЂ Р ВµР Р…Р Вµ
    lastScene_->setCameraPosition(cameraPos);
    if (!lastScene_->supportsTerrainVisibility()) {
        terrainIndexCount_ = 0;
        return;
    }

    // Р Р€Р вЂќР С’Р вЂєР ВР СћР В¬ РЎРЊРЎвЂљРЎС“ РЎРѓРЎвЂљРЎР‚Р С•Р С”РЎС“:
    // lastScene_->updatePrediction(cameraPos);

    // Р ВРЎРѓР С—Р С•Р В»РЎРЉР В·РЎС“Р ВµР С Р В°Р Т‘Р В°Р С—РЎвЂљР С‘Р Р†Р Р…РЎС“РЎР‹ Р В»Р С•Р С–Р С‘Р С”РЎС“ Р Т‘Р В»РЎРЏ Р С•Р С—РЎР‚Р ВµР Т‘Р ВµР В»Р ВµР Р…Р С‘РЎРЏ Р Р…Р ВµР С•Р В±РЎвЂ¦Р С•Р Т‘Р С‘Р СР С•РЎРѓРЎвЂљР С‘ Р С•Р В±Р Р…Р С•Р Р†Р В»Р ВµР Р…Р С‘РЎРЏ
    if (lastScene_->shouldUpdateVisibility() && lastScene_->hasCameraMoved(0.1f)) {
        QElapsedTimer filterTimer;
        filterTimer.start();

        // Р ВР РЋР СџР В Р С’Р вЂ™Р ВР СћР В¬: Р С‘РЎРѓР С—Р С•Р В»РЎРЉР В·Р С•Р Р†Р В°РЎвЂљРЎРЉ Р С•Р В±РЎвЂ№РЎвЂЎР Р…РЎС“РЎР‹ Р Р†Р ВµРЎР‚РЎРѓР С‘РЎР‹, Р Р…Р Вµ РЎРѓ Р С—РЎР‚Р ВµР Т‘РЎРѓР С”Р В°Р В·Р В°Р Р…Р С‘Р ВµР С
        std::vector<uint32_t> visibleIndices = lastScene_->getVisibleIndices(cameraPos);

        qint64 elapsed = filterTimer.elapsed();

        // Р РЋРЎвЂљР В°РЎвЂљР С‘РЎРѓРЎвЂљР С‘Р С”Р В°
        static int updateCount = 0;
        static qint64 totalTime = 0;
        updateCount++;
        totalTime += elapsed;

        if (updateCount % 10 == 0) {
            // Отключено для производительности
        }

        // Р С›Р В±Р Р…Р С•Р Р†Р В»РЎРЏР ВµР С Р С‘Р Р…Р Т‘Р ВµР С”РЎРѓР Р…РЎвЂ№Р в„– Р В±РЎС“РЎвЂћР ВµРЎР‚
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

    // Р вЂўРЎРѓР В»Р С‘ VAO РЎС“Р В¶Р Вµ РЎРѓР С•Р В·Р Т‘Р В°Р Р…, Р Р…Р Вµ РЎРѓР С•Р В·Р Т‘Р В°Р ВµР С Р В·Р В°Р Р…Р С•Р Р†Р С•
    if (vaoTerrain_.isCreated()) {
        qDebug() << "VAO already created, skipping recreation";
        return;
    }

    qDebug() << "Creating terrain VAO - START";

    // Р РЋР С•Р В·Р Т‘Р В°Р ВµР С Р Р…Р С•Р Р†РЎвЂ№Р в„– VAO
    if (!vaoTerrain_.create()) {
        qDebug() << "Failed to create VAO!";
        return;
    }

    vaoTerrain_.bind();

    // Р СњР В°РЎРѓРЎвЂљРЎР‚Р В°Р С‘Р Р†Р В°Р ВµР С Р В°РЎвЂљРЎР‚Р С‘Р В±РЎС“РЎвЂљРЎвЂ№
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(1);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(2);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainOre_);
    gl_->glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(3);

    // Р СџРЎР‚Р С‘Р Р†РЎРЏР В·РЎвЂ№Р Р†Р В°Р ВµР С Р С‘Р Р…Р Т‘Р ВµР С”РЎРѓР Р…РЎвЂ№Р в„– Р В±РЎС“РЎвЂћР ВµРЎР‚
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);

    vaoTerrain_.release();

    // Р С›Р В±Р Р…Р С•Р Р†Р В»РЎРЏР ВµР С VAO Р Р† РЎР‚Р ВµР Р…Р Т‘Р ВµРЎР‚Р ВµРЎР‚Р Вµ
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

    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(uint32_t), data.indices.empty() ? nullptr : data.indices.data(), GL_STATIC_DRAW);

    gl_->glBindVertexArray(vaoWater_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);

    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    gl_->glBindVertexArray(0);

    waterIndexCount_ = static_cast<GLsizei>(data.indices.size());
}

void HexSphereRenderer::rebuildPlanetSurfaceAtlas(
    const TerrainMesh& mesh,
    const HexSphereModel& model) {
    if (!surfaceAtlasPass_) return;
    surfaceAtlasPass_->updateMesh(mesh, model);
    planetRadiusAtlas_ = surfaceAtlasPass_->radiusTexture();
    planetSurfaceKindAtlas_ = surfaceAtlasPass_->kindTexture();
    planetShoreDistanceAtlas_ = surfaceAtlasPass_->shoreDistanceTexture();
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
        rebuildPlanetSurfaceAtlas(scene.terrain(), scene.model());
        uploadSelectionOutlineInternal(scene.buildSelectionOutlineVertices());
        uploadPathInternal({});
        if (uploadedWaterProxyRevision_ != scene.waterProxyRevision()) {
            uploadWaterInternal(scene.waterGeometry());
            uploadedWaterProxyRevision_ = scene.waterProxyRevision();
        }
        });
    qDebug() << "Buffer strategy:" << (options.useStaticBuffers ? "STATIC" : "DYNAMIC")
        << "(terrain" << options.terrainUsage << ", wire" << options.wireUsage << ")";
}

void HexSphereRenderer::renderScene(const RenderGraph& graph, const RenderCamera& camera, const SceneLighting& lighting) {
    if (!glReady_) return;

    static QElapsedTimer frameTimer;
    static int frameCount = 0;
    static qint64 totalTime = 0;

    if (!frameTimer.isValid()) {
        frameTimer.start();
    }

    QVector3D cameraPos = (camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

    if (!terrainRenderer_ || !waterRenderer_ || !entityRenderer_ || !overlayRenderer_) {
        qDebug() << "ERROR: One or more renderers are null!";
        return;
    }

    updateVisibility(cameraPos);

    const float dpr = owner_->devicePixelRatioF();
    const int viewportWidth = int(owner_->width() * dpr);
    const int viewportHeight = int(owner_->height() * dpr);
    gl_->glViewport(0, 0, viewportWidth, viewportHeight);
    gl_->glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

    const QMatrix4x4 viewProjection = camera.projection * camera.view;
    RenderContext ctx{
        graph, camera, lighting, viewProjection, viewProjection.inverted(),
        cameraPos, QSize(viewportWidth, viewportHeight) };

    if (surfaceAtlasPass_) surfaceAtlasPass_->renderIfDirty();
    gl_->glViewport(0, 0, viewportWidth, viewportHeight);
    terrainRenderer_->render(ctx, terrainIndexCount_);
    copySceneDepthToTexture(viewportWidth, viewportHeight);
    const WaterRenderer::Resources waterResources{
        envCubemap_, sceneDepthTexture_, planetRadiusAtlas_,
        planetSurfaceKindAtlas_, planetShoreDistanceAtlas_,
        vaoWater_, waterIndexCount_ };
    waterRenderer_->render(ctx, waterResources);
    entityRenderer_->renderEntities(ctx);
    overlayRenderer_->render(ctx);

    if (roadVertexCount_ > 0 && progTerrain_ != 0) {
        gl_->glUseProgram(progTerrain_);

        gl_->glUniformMatrix4fv(uMVP_Terrain_, 1, GL_FALSE, ctx.mvp.constData());

        QMatrix4x4 model;
        model.setToIdentity();
        gl_->glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.constData());
        gl_->glUniformMatrix3fv(uNormalMatrix_, 1, GL_FALSE, model.normalMatrix().constData());

        const QVector3D& lightDir = ctx.lighting.direction;
        gl_->glUniform3f(uLightDir_, lightDir.x(), lightDir.y(), lightDir.z());

        if (uRoadColor_ >= 0) {
            gl_->glUniform3f(uRoadColor_, 0.35f, 0.32f, 0.28f);
        }

        if (uIsRoad_ >= 0) {
            gl_->glUniform1i(uIsRoad_, 1);
        }

        gl_->glBindVertexArray(vaoRoad_);
        gl_->glDrawArrays(GL_TRIANGLES, 0, roadVertexCount_);
        gl_->glBindVertexArray(0);

        if (uIsRoad_ >= 0) {
            gl_->glUniform1i(uIsRoad_, 0);
        }
    }

    if (graph.scene.isContributorMode()) {
        renderContributorModel(ctx);
    }
    else {
        renderProceduralTreeParticles(ctx);
    }

    if (stats_) stats_->stopGPUTimer();

    frameCount++;
    totalTime += frameTimer.restart();

    if (frameCount >= 60) {
        const float fps = 60.0f / (totalTime / 1000.0f);
        const float avgMs = static_cast<float>(totalTime) / 60.0f;
        qDebug() << "FPS:" << fps << "AVG frame:" << avgMs << "ms";
        frameCount = 0;
        totalTime = 0;
    }
}

void HexSphereRenderer::renderProceduralTreeParticles(const RenderContext& ctx) {
    if (!particleRenderer_ || !particleRenderer_->isInitialized()) return;
    if (treeParticlesBySpecies.empty()) return;
    if (proceduralTreeModelsBySpecies.empty()) return;
    if (speciesVariants.empty()) return;

    // ========== ВРЕМЯ ==========
    static QElapsedTimer animTimer;
    if (!animTimer.isValid()) animTimer.start();
    const float dt = std::min(static_cast<float>(animTimer.restart()) / 1000.0f, 0.1f);
    static float treeParticleTime = 0.0f;
    treeParticleTime += dt;

    // ========== СРЕДНИЙ ВЕТЕР ==========
    windField_.direction = QVector3D(0.8f, 0.2f, 0.4f).normalized();
    windField_.strength = 0.20f;
    windField_.gustStrength = 0.22f;
    windField_.gustSpeed = 1.1f;
    windField_.turbulence = 0.09f;

    particleRenderer_->update(dt, windField_, QVector3D(0, 0, 0));
    particleRenderer_->setTime(treeParticleTime);

    const auto& placements = ctx.graph.scene.getTreePlacements();
    if (placements.empty()) return;

    // ========== ГЛОБАЛЬНЫЙ МАСШТАБ ==========
    constexpr float kTreeGlobalScale = 0.12f;

    // ========== БЮДЖЕТ ЧАСТИЦ ==========
    constexpr size_t kMaxParticlesTotal = 60000;
    constexpr size_t kMinParticlesPerTree = 1000;
    constexpr size_t kMaxParticlesPerTree = 5000;

    const size_t treeCount = placements.size();
    const size_t computedPerTree = kMaxParticlesTotal / std::max<size_t>(treeCount, 1);
    const size_t particlesPerTreeBudget = std::clamp<size_t>(
        computedPerTree, kMinParticlesPerTree, kMaxParticlesPerTree);

    // ========== ХЭШ ДЛЯ ПЕРЕСОЗДАНИЯ ==========
    uint64_t placementHash = 1469598103934665603ull;
    auto hashCombine = [&placementHash](uint64_t value) {
        placementHash ^= value;
        placementHash *= 1099511628211ull;
        };

    const auto& model = ctx.graph.scene.model();
    const auto& cells = model.cells();

    // Считаем индекс вида и варианта для каждого placement
    struct PlacementInfo {
        TreeBuilder::TreeSpecies species;
        int speciesIdx;
        int variantIdx;
        QVector3D treePos;
        QVector3D up;
    };
    std::vector<PlacementInfo> infos;
    infos.reserve(treeCount);

    hashCombine(static_cast<uint64_t>(treeCount));

    for (size_t i = 0; i < treeCount; ++i) {
        const auto& placement = placements[i];
        const QVector3D treePos = computeSurfacePoint(
            ctx.graph.scene, placement, ctx.graph.heightStep);
        const QVector3D up = treePos.normalized();

        // Биом клетки
        Biome biome = Biome::Grass;
        if (placement.cellId >= 0 &&
            placement.cellId < static_cast<int>(cells.size())) {
            biome = cells[static_cast<size_t>(placement.cellId)].biome;
        }

        // Вид по биому + seed
        const uint32_t speciesSeed =
            static_cast<uint32_t>(placement.cellId) * 2654435761u;
        const auto species = TreeBuilder::selectSpecies(biome, speciesSeed);
        const int speciesIdx = static_cast<int>(species);

        // Вариант
        const size_t variantsForSpecies =
            speciesVariants[speciesIdx].size();
        if (variantsForSpecies == 0) {
            continue;
        }

        const uint32_t variantSeed =
            static_cast<uint32_t>(placement.cellId) * 40503u ^
            static_cast<uint32_t>(placement.triangleIdx) * 2246822519u ^
            static_cast<uint32_t>(i) * 3266489917u;
        const int variantIdx = static_cast<int>(variantSeed % variantsForSpecies);

        infos.push_back({ species, speciesIdx, variantIdx, treePos, up });

        // Хэш
        hashCombine(static_cast<uint64_t>(placement.cellId + 10007));
        hashCombine(static_cast<uint64_t>(speciesIdx));
        hashCombine(static_cast<uint64_t>(variantIdx));
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

    // ========== ПЕРЕСОЗДАНИЕ ЧАСТИЦ ==========
    if (placementHash != treeParticlesPlacementHash_) {
        std::vector<ContributorParticle> worldParticles;
        worldParticles.reserve(treeCount * kMaxParticlesPerTree);

        for (const auto& info : infos) {
            const auto& variant = speciesVariants[info.speciesIdx][info.variantIdx];

            const auto& particleTemplate =
                treeParticlesBySpecies[info.speciesIdx][info.variantIdx];
            if (particleTemplate.empty()) continue;

            // Итоговый масштаб: реалистичный × глобальный × per-placement
            const float baseScale =
                variant.realisticScale * kTreeGlobalScale;
            const float finalScale = baseScale * placements[
                &info - infos.data()].scale;

            QMatrix4x4 transform;
            transform.translate(info.treePos);
            orientTreeToSurface(transform, info.up);

            // rotation из placement — но нужно найти соответствующий
            const size_t idx = static_cast<size_t>(&info - infos.data());
            transform.rotate(
                placements[idx].rotation * 180.0f / 3.14159265f, 0, 1, 0);
            transform.scale(finalScale);

            const QVector3D foliageColor = variant.foliageColor;

            const size_t sourceStep = std::max<size_t>(
                1, particleTemplate.size() / particlesPerTreeBudget);
            size_t emittedForTree = 0;

            for (size_t srcIdx = 0;
                srcIdx < particleTemplate.size();
                srcIdx += sourceStep) {
                if (emittedForTree >= particlesPerTreeBudget) break;

                const auto& source = particleTemplate[srcIdx];
                ContributorParticle particle = source;
                particle.restPosition =
                    (transform * QVector4D(source.restPosition, 1.0f)).toVector3D();
                particle.position =
                    (transform * QVector4D(source.position, 1.0f)).toVector3D();
                particle.normal =
                    transform.mapVector(source.normal).normalized();
                particle.color = foliageColor;
                particle.size *= 3.0f * finalScale;
                particle.velocity = QVector3D(0, 0, 0);
                particle.windWeight = 0.6f;

                worldParticles.push_back(particle);
                ++emittedForTree;
            }
        }

        if (!worldParticles.empty()) {
            qDebug() << "Updating procedural tree particles:"
                << worldParticles.size() << "particles for"
                << treeCount << "trees";
            particleRenderer_->updateParticles(worldParticles);
            treeParticlesPlacementHash_ = placementHash;
        }
    }

    particleRenderer_->render(ctx.mvp, ctx.camera.view, ctx.cameraPos);

    // ===================================================================
    // ================ РИСУЕМ СТВОЛЫ ====================================
    // ===================================================================
    gl_->glUseProgram(progModel_);

    const GLint uIsCar = gl_->glGetUniformLocation(progModel_, "uIsCar");
    if (uIsCar >= 0) gl_->glUniform1i(uIsCar, 0);

    const GLint uUseFoliageColor =
        gl_->glGetUniformLocation(progModel_, "uUseFoliageColor");
    if (uUseFoliageColor >= 0) gl_->glUniform1i(uUseFoliageColor, 0);

    const GLint uTrunkColor =
        gl_->glGetUniformLocation(progModel_, "uTrunkColor");

    const QVector3D globalLightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
    const QVector3D eye =
        (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

    gl_->glUniform3f(uLightDir_Model_,
        globalLightDir.x(), globalLightDir.y(), globalLightDir.z());
    gl_->glUniform3f(uViewPos_Model_, eye.x(), eye.y(), eye.z());

    for (size_t i = 0; i < infos.size(); ++i) {
        const auto& info = infos[i];
        const auto& placement = placements[i];
        const auto& variant = speciesVariants[info.speciesIdx][info.variantIdx];

        const auto& model3d =
            proceduralTreeModelsBySpecies[info.speciesIdx][info.variantIdx];
        if (!model3d || !model3d->isInitialized()) continue;

        const float baseScale = variant.realisticScale * kTreeGlobalScale;
        const float finalScale = baseScale * placement.scale;

        QMatrix4x4 modelMat;
        modelMat.translate(info.treePos);
        orientTreeToSurface(modelMat, info.up);
        modelMat.rotate(placement.rotation * 180.0f / 3.14159265f, 0, 1, 0);
        modelMat.scale(finalScale);

        const QMatrix4x4 mvpTree =
            ctx.camera.projection * ctx.camera.view * modelMat;

        if (uTrunkColor >= 0) {
            gl_->glUniform3f(uTrunkColor,
                variant.trunkColor.x(),
                variant.trunkColor.y(),
                variant.trunkColor.z());
        }

        model3d->draw(progModel_, mvpTree, modelMat, ctx.camera.view,
            variant.trunkColor, /*forceTextureOff=*/true);
    }

    // Возвращаем состояние
    if (uUseFoliageColor >= 0) gl_->glUniform1i(uUseFoliageColor, 0);
    gl_->glUseProgram(0);
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

void HexSphereRenderer::uploadTerrainHydrology(
    const HexSphereSceneController& scene,
    GLenum terrainUsage) {
    withContext([&]() {
        uploadTerrainInternal(scene.terrain(), terrainUsage);
        rebuildPlanetSurfaceAtlas(scene.terrain(), scene.model());
        });
}

void HexSphereRenderer::ensureSceneDepthTexture(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (sceneDepthTexture_ == 0) gl_->glGenTextures(1, &sceneDepthTexture_);
    if (sceneDepthFbo_ == 0) gl_->glGenFramebuffers(1, &sceneDepthFbo_);
    if (sceneDepthTextureSize_ == QSize(width, height)) return;

    gl_->glBindTexture(GL_TEXTURE_2D, sceneDepthTexture_);
    gl_->glTexImage2D(
        GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl_->glBindFramebuffer(GL_FRAMEBUFFER, sceneDepthFbo_);
    gl_->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepthTexture_, 0);
    gl_->glDrawBuffer(GL_NONE);
    gl_->glReadBuffer(GL_NONE);
    const GLenum status = gl_->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qWarning() << "Scene depth framebuffer is incomplete:" << status;
    }
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, owner_ ? owner_->defaultFramebufferObject() : 0);
    gl_->glBindTexture(GL_TEXTURE_2D, 0);
    sceneDepthTextureSize_ = QSize(width, height);
}

void HexSphereRenderer::copySceneDepthToTexture(int width, int height) {
    ensureSceneDepthTexture(width, height);
    if (!sceneDepthTexture_ || !sceneDepthFbo_) return;
    const GLuint defaultFbo = owner_ ? owner_->defaultFramebufferObject() : 0;
    gl_->glBindFramebuffer(GL_READ_FRAMEBUFFER, defaultFbo);
    gl_->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneDepthFbo_);
    gl_->glBlitFramebuffer(
        0, 0, width, height, 0, 0, width, height,
        GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    gl_->glBindFramebuffer(GL_READ_FRAMEBUFFER, defaultFbo);
    gl_->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFbo);
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

void HexSphereRenderer::setOreVisualizationEnabled(bool enabled) {
    oreVisualizationEnabled_ = enabled;
    if (terrainRenderer_) {
        terrainRenderer_->setOreVisualizationEnabled(enabled);
    }
}

void HexSphereRenderer::uploadRoad(const std::vector<float>& vertices) {
    withContext([&]() {
        if (vertices.empty()) {
            roadVertexCount_ = 0;
            gl_->glBindBuffer(GL_ARRAY_BUFFER, vboRoad_);
            gl_->glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
            return;
        }

        gl_->glBindBuffer(GL_ARRAY_BUFFER, vboRoad_);
        gl_->glBufferData(GL_ARRAY_BUFFER,
            vertices.size() * sizeof(float),
            vertices.data(),
            GL_DYNAMIC_DRAW);
        roadVertexCount_ = GLsizei(vertices.size() / 3);
        });
}


void HexSphereRenderer::updateTerrainOreData(const TerrainMesh& mesh) {
    withContext([&]() {
        if (mesh.ore.empty() || vboTerrainOre_ == 0) {
            return;
        }

        // РћР±РЅРѕРІР»СЏРµРј С‚РѕР»СЊРєРѕ ore-Р±СѓС„РµСЂ
        gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainOre_);
        gl_->glBufferSubData(GL_ARRAY_BUFFER, 0,
            mesh.ore.size() * sizeof(float),
            mesh.ore.data());
        gl_->glBindBuffer(GL_ARRAY_BUFFER, 0);
        });
}
