#include "renderers/HexSphereRenderer.h"

#include <QOpenGLWidget>
#include <QtDebug>

#include <QOpenGLVertexArrayObject> 

#include "resources/HexSphereWidget_shaders.h"
#include "model/SurfacePlacement.h"
#include "renderers/EntityRenderer.h"
#include "ui/OverlayRenderer.h"
#include "renderers/TerrainRenderer.h"
#include "renderers/WaterRenderer.h"

HexSphereRenderer::HexSphereRenderer(QOpenGLWidget* owner)
    : owner_(owner) {}

HexSphereRenderer::~HexSphereRenderer() {
    if (!glReady_) {
        return;
    }

    // Проверяем, что контекст ещё существует
    if (!owner_ || !gl_) {
        return;
    }

    owner_->makeCurrent();

    // 1. Сначала удаляем рендереры (они используют OpenGL)
    terrainRenderer_.reset();
    waterRenderer_.reset();
    entityRenderer_.reset();
    overlayRenderer_.reset();

    // 2. Очищаем модель деревьев
    if (treeModel_.use_count() == 1 && treeModel_) {
        treeModel_->clearGPUResources();
    }

    // 3. Удаляем шейдерные программы
    if (progWire_)    gl_->glDeleteProgram(progWire_);
    if (progTerrain_) gl_->glDeleteProgram(progTerrain_);
    if (progSel_)     gl_->glDeleteProgram(progSel_);
    if (progWater_)   gl_->glDeleteProgram(progWater_);
    if (progModel_)   gl_->glDeleteProgram(progModel_);

    // 4. Удаляем VAO (кроме vaoTerrain_ - он удалится автоматически)
    if (vaoWire_ != 0)     gl_->glDeleteVertexArrays(1, &vaoWire_);
    if (vaoSel_ != 0)      gl_->glDeleteVertexArrays(1, &vaoSel_);
    if (vaoWater_ != 0)    gl_->glDeleteVertexArrays(1, &vaoWater_);
    if (vaoPyramid_ != 0)  gl_->glDeleteVertexArrays(1, &vaoPyramid_);

    // 5. Явно уничтожаем QOpenGLVertexArrayObject
    if (vaoTerrain_.isCreated()) {
        // Убеждаемся, что VAO не привязан
        gl_->glBindVertexArray(0);
        vaoTerrain_.destroy();
    }

    // 6. Удаляем буферы
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

    owner_->doneCurrent();
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

    const QString modelPath = "Planet/tree.obj";
    treeModel_ = ModelHandler::loadShared(modelPath);
    if (treeModel_) {
        treeModel_->uploadToGPU();
    }

    // СОЗДАЁМ РЕНДЕРЕРЫ ПОСЛЕ ВСЕХ ИНИЦИАЛИЗАЦИЙ
    terrainRenderer_ = std::make_unique<TerrainRenderer>(
        gl_,
        progTerrain_,
        uMVP_Terrain_,
        uModel_,
        uLightDir_,
        uNormalMatrix_,
        vaoTerrain_.objectId()  // ← objectId() возвращает GLuint
    );

    waterRenderer_ = std::make_unique<WaterRenderer>(gl_, progWater_, uMVP_Water_, uTime_Water_, uLightDir_Water_, uViewPos_Water_, uEnvMap_, envCubemap_, vaoWater_, waterIndexCount_);
    entityRenderer_ = std::make_unique<EntityRenderer>(gl_, progWire_, progSel_, progModel_, uMVP_Wire_, uMVP_Sel_, uMVP_Model_, uModel_Model_, uLightDir_Model_, uViewPos_Model_, uColor_Model_, uUseTexture_, vaoPyramid_, pyramidVertexCount_, treeModel_);
    overlayRenderer_ = std::make_unique<OverlayRenderer>(gl_, progWire_, progSel_, uMVP_Wire_, uMVP_Sel_, vaoWire_, vaoSel_, vaoPath_, lineVertexCount_, selLineVertexCount_, pathVertexCount_);

    glReady_ = true;
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

    // Загружаем вершины
    const GLsizeiptr vbPos = GLsizeiptr(mesh.pos.size() * sizeof(float));
    const GLsizeiptr vbCol = GLsizeiptr(mesh.col.size() * sizeof(float));
    const GLsizeiptr vbNorm = GLsizeiptr(mesh.norm.size() * sizeof(float));

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbPos, mesh.pos.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbCol, mesh.col.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbNorm, mesh.norm.data(), usage);

    // Получаем индексы из сцены (с фильтрацией)
    std::vector<uint32_t> indicesToUse;

    if (lastScene_) {
        qDebug() << "Getting visible indices from scene";
        QVector3D cameraPos = lastScene_->getCameraPosition();
        indicesToUse = lastScene_->getVisibleIndices(cameraPos);
        qDebug() << "Visible indices:" << indicesToUse.size();
    }
    else {
        qDebug() << "No scene, using all indices from mesh";
        indicesToUse = mesh.idx;
    }

    // Загружаем индексы
    const GLsizeiptr ib = GLsizeiptr(indicesToUse.size() * sizeof(uint32_t));
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib,
        indicesToUse.empty() ? nullptr : indicesToUse.data(),
        GL_DYNAMIC_DRAW);

    terrainIndexCount_ = GLsizei(indicesToUse.size());

    // ВАЖНО: Пересоздаём VAO после загрузки всех данных!
    recreateTerrainVAO();

    qDebug() << "uploadTerrainInternal - final indexCount:" << terrainIndexCount_;

    if (stats_) {
        stats_->updateMemoryStats(GLsizei(mesh.pos.size() / 3),
            terrainIndexCount_,
            terrainIndexCount_ / 3);
    }
}

// ========== НОВЫЙ МЕТОД ДЛЯ ОБНОВЛЕНИЯ ВИДИМОСТИ ==========
void HexSphereRenderer::updateVisibility(const QVector3D& cameraPos) {
    if (!glReady_ || !lastScene_) return;

    lastScene_->setCameraPosition(cameraPos);

    if (lastScene_->hasCameraMoved()) {
        TerrainMesh visibleMesh = lastScene_->getVisibleTerrainMesh();

        if (visibleMesh.idx.empty()) {
            qDebug() << "No visible triangles!";
            return;
        }

        qDebug() << "Updating visibility - new indices:" << visibleMesh.idx.size()
            << "camera dist:" << cameraPos.length();

        withContext([&]() {
            // Обновляем индексный буфер
            gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
            gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                visibleMesh.idx.size() * sizeof(uint32_t),
                visibleMesh.idx.data(),
                GL_DYNAMIC_DRAW);

            // Пересоздаём VAO
            recreateTerrainVAO();
            });

        terrainIndexCount_ = GLsizei(visibleMesh.idx.size());
        lastScene_->updateLastCameraPosition();

        qDebug() << "Visibility updated, new index count:" << terrainIndexCount_;
    }
}

void HexSphereRenderer::recreateTerrainVAO() {
    if (!glReady_ || !gl_) {
        qDebug() << "OpenGL not ready!";
        return;
    }

    qDebug() << "Recreating terrain VAO - START";

    // Удаляем старый VAO если есть
    if (vaoTerrain_.isCreated()) {
        gl_->glBindVertexArray(0);
        vaoTerrain_.destroy();
    }

    // Создаём новый VAO
    if (!vaoTerrain_.create()) {
        qDebug() << "Failed to create VAO!";
        return;
    }

    // bind() не возвращает значение, просто вызываем его
    vaoTerrain_.bind();

    // Привязываем буферы
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    qDebug() << "Position attribute set";

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(1);
    qDebug() << "Color attribute set";

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(2);
    qDebug() << "Normal attribute set";

    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    qDebug() << "Index buffer bound";

    vaoTerrain_.release();

    // Обновляем VAO в TerrainRenderer
    if (terrainRenderer_) {
        GLuint vaoId = vaoTerrain_.objectId();
        terrainRenderer_->updateVAO(vaoId);
        qDebug() << "Updated TerrainRenderer VAO to:" << vaoId;
    }

    qDebug() << "Recreating terrain VAO - END";
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

void HexSphereRenderer::uploadScene(const HexSphereSceneController& scene, const UploadOptions& options) {
    qDebug() << "uploadScene called, setting lastScene_";
    lastScene_ = const_cast<HexSphereSceneController*>(&scene);

    withContext([&]() {
        uploadWireInternal(scene.buildWireVertices(), options.wireUsage);
        uploadTerrainInternal(scene.terrain(), options.terrainUsage);
        uploadSelectionOutlineInternal(scene.buildSelectionOutlineVertices());
        if (auto path = scene.buildPathPolyline()) {
            uploadPathInternal(*path);
        } else {
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

    // Проверяем, что рендереры существуют
    if (!terrainRenderer_ || !waterRenderer_ || !entityRenderer_ || !overlayRenderer_) {
        qDebug() << "ERROR: One or more renderers are null!";
        return;
    }

    // Обновляем видимость
    updateVisibility(cameraPos);

    const float dpr = owner_->devicePixelRatioF();
    gl_->glViewport(0, 0, int(owner_->width() * dpr), int(owner_->height() * dpr));
    gl_->glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (stats_) stats_->startGPUTimer();

    RenderContext ctx{ graph, camera, lighting, camera.projection * camera.view, cameraPos };

    terrainRenderer_->render(ctx, terrainIndexCount_);
    waterRenderer_->render(ctx);
    entityRenderer_->renderEntities(ctx);
    overlayRenderer_->render(ctx);
    entityRenderer_->renderTrees(ctx);

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
    // Здесь можно передать время в тесселятор, если нужно
}

void HexSphereRenderer::setOreVisualizationEnabled(bool enabled) {
    oreVisualizationEnabled_ = enabled;
    // Здесь можно обновить состояние рендерера
}
