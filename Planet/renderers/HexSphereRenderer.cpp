#include "renderers/HexSphereRenderer.h"

#include <QOpenGLWidget>
#include <QtDebug>

#include "resources/HexSphereWidget_shaders.h"
#include "model/SurfacePlacement.h"
#include "renderers/EntityRenderer.h"
#include "ui/OverlayRenderer.h"
#include "renderers/TerrainRenderer.h"
#include "renderers/WaterRenderer.h"
#include "../DebugMacros.h"

HexSphereRenderer::HexSphereRenderer(QOpenGLWidget* owner)
    : owner_(owner) {}

HexSphereRenderer::~HexSphereRenderer() {
    if (!glReady_) {
        return;
    }

    owner_->makeCurrent();

    if (treeModel_.use_count() == 1 && treeModel_) {
        treeModel_->clearGPUResources();
    }

    if (progWire_)    gl_->glDeleteProgram(progWire_);
    if (progTerrain_) gl_->glDeleteProgram(progTerrain_);
    if (progSel_)     gl_->glDeleteProgram(progSel_);
    if (progWater_)   gl_->glDeleteProgram(progWater_);
    if (progModel_)   gl_->glDeleteProgram(progModel_);

    if (vaoWire_)     gl_->glDeleteVertexArrays(1, &vaoWire_);
    if (vaoTerrain_)  gl_->glDeleteVertexArrays(1, &vaoTerrain_);
    if (vaoSel_)      gl_->glDeleteVertexArrays(1, &vaoSel_);
    if (vaoWater_)    gl_->glDeleteVertexArrays(1, &vaoWater_);
    if (vaoPyramid_)  gl_->glDeleteVertexArrays(1, &vaoPyramid_);

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

void HexSphereRenderer::updateIndexCounts(const TerrainMesh& mesh, const WaterGeometryData& water) {
    if (terrainRenderer_) {
        terrainRenderer_->updateIndexCount(GLsizei(mesh.idx.size()));
    }
    if (waterRenderer_) {
        waterRenderer_->updateIndexCount(GLsizei(water.indices.size()));
    }
    // Обновляем другие счетчики при необходимости
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

    // 1. Базовые настройки OpenGL
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glEnable(GL_CULL_FACE);
    gl_->glCullFace(GL_BACK);
    gl_->glFrontFace(GL_CCW);

    // 2. Создание шейдеров
    if (!createShaders()) {
        DEBUG_CALL_PARAM("ERROR: Failed to create shaders");
        return;
    }

    // 3. Настройка uniform locations
    setupUniformLocations();

    // 4. Генерация cubemap для воды
    generateEnvCubemap();

    // 5. Создание буферов и VAO
    createBuffersAndVAOs();

    // 6. Настройка всех VAO
    setupWireVAO();
    setupTerrainVAO();
    setupSelectionVAO();
    setupPathVAO();
    setupWaterVAO();

    // 7. Инициализация геометрии пирамиды
    initPyramidGeometry();

    // 8. Загрузка модели дерева
    loadTreeModel();

    // 9. Создание рендереров
    createRenderers();

    DEBUG_CALL_PARAM("initialization complete");
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
    DEBUG_CALL_PARAM("vertices=" << mesh.pos.size() / 3
        << " triangles=" << mesh.idx.size() / 3
        << " usage=" << (usage == GL_STATIC_DRAW ? "STATIC" : "DYNAMIC"));

    if (stats_) stats_->startGPUTimer();

    // ============== ПРОВЕРКА 1: Валидность данных ==============
    if (mesh.pos.empty() || mesh.idx.empty() || mesh.col.empty() || mesh.norm.empty()) {
        DEBUG_CALL_PARAM("ERROR: empty mesh data!");
        DEBUG_CALL_PARAM("  pos empty: " << mesh.pos.empty());
        DEBUG_CALL_PARAM("  col empty: " << mesh.col.empty());
        DEBUG_CALL_PARAM("  norm empty: " << mesh.norm.empty());
        DEBUG_CALL_PARAM("  idx empty: " << mesh.idx.empty());
        return;
    }

    // ============== ПРОВЕРКА 2: Консистентность размеров ==============
    if (mesh.pos.size() != mesh.col.size() || mesh.pos.size() != mesh.norm.size()) {
        DEBUG_CALL_PARAM("ERROR: Size mismatch!");
        DEBUG_CALL_PARAM("  pos.size()  = " << mesh.pos.size() << " (" << mesh.pos.size() / 3 << " vertices)");
        DEBUG_CALL_PARAM("  col.size()  = " << mesh.col.size() << " (" << mesh.col.size() / 3 << " colors)");
        DEBUG_CALL_PARAM("  norm.size() = " << mesh.norm.size() << " (" << mesh.norm.size() / 3 << " normals)");
        return;
    }

    // ============== ПРОВЕРКА 3: Индексы в пределах диапазона ==============
    uint32_t maxIndex = 0;
    uint32_t minIndex = UINT32_MAX;
    for (uint32_t idx : mesh.idx) {
        if (idx > maxIndex) maxIndex = idx;
        if (idx < minIndex) minIndex = idx;
    }
    uint32_t vertexCount = static_cast<uint32_t>(mesh.pos.size() / 3);

    if (maxIndex >= vertexCount) {
        DEBUG_CALL_PARAM("ERROR: Index out of range!");
        DEBUG_CALL_PARAM("  maxIndex = " << maxIndex << " (should be < " << vertexCount << ")");
        DEBUG_CALL_PARAM("  minIndex = " << minIndex);
        DEBUG_CALL_PARAM("  vertexCount = " << vertexCount);
        return;
    }

    DEBUG_CALL_PARAM("Index validation passed: min=" << minIndex << " max=" << maxIndex << " vertexCount=" << vertexCount);

    // ============== ПРОВЕРКА 4: OpenGL контекст ==============
    if (!gl_ || !gl_->glGetString(GL_VERSION)) {
        DEBUG_CALL_PARAM("ERROR: No valid OpenGL context!");
        return;
    }

    // ============== ПРОВЕРКА 5: VAO и буферы созданы ==============
    if (vaoTerrain_ == 0 || vboTerrainPos_ == 0 || vboTerrainCol_ == 0 ||
        vboTerrainNorm_ == 0 || iboTerrain_ == 0) {
        DEBUG_CALL_PARAM("ERROR: Buffers or VAO not created!");
        DEBUG_CALL_PARAM("  vaoTerrain_ = " << vaoTerrain_);
        DEBUG_CALL_PARAM("  vboTerrainPos_ = " << vboTerrainPos_);
        DEBUG_CALL_PARAM("  vboTerrainCol_ = " << vboTerrainCol_);
        DEBUG_CALL_PARAM("  vboTerrainNorm_ = " << vboTerrainNorm_);
        DEBUG_CALL_PARAM("  iboTerrain_ = " << iboTerrain_);
        return;
    }

    // ============== РАЗМЕРЫ БУФЕРОВ ==============
    const GLsizeiptr vbPos = GLsizeiptr(mesh.pos.size() * sizeof(float));
    const GLsizeiptr vbCol = GLsizeiptr(mesh.col.size() * sizeof(float));
    const GLsizeiptr vbNorm = GLsizeiptr(mesh.norm.size() * sizeof(float));
    const GLsizeiptr ib = GLsizeiptr(mesh.idx.size() * sizeof(uint32_t));

    DEBUG_CALL_PARAM("buffer sizes (bytes):");
    DEBUG_CALL_PARAM("  pos  = " << vbPos << " (" << mesh.pos.size() << " floats)");
    DEBUG_CALL_PARAM("  col  = " << vbCol << " (" << mesh.col.size() << " floats)");
    DEBUG_CALL_PARAM("  norm = " << vbNorm << " (" << mesh.norm.size() << " floats)");
    DEBUG_CALL_PARAM("  idx  = " << ib << " (" << mesh.idx.size() << " uint32)");

    // ============== ОЧИСТКА ПРЕДЫДУЩИХ ОШИБОК ==============
    GLenum clearErr;
    int errorCount = 0;
    while ((clearErr = gl_->glGetError()) != GL_NO_ERROR) {
        errorCount++;
        DEBUG_CALL_PARAM("Cleared previous OpenGL error: " << clearErr);
    }
    if (errorCount > 0) {
        DEBUG_CALL_PARAM("Cleared " << errorCount << " previous OpenGL errors");
    }

    // ============== ЗАГРУЗКА ПОЗИЦИЙ ==============
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbPos, mesh.pos.data(), usage);

    GLenum err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("ERROR: OpenGL Error after position buffer: " << err);
        DEBUG_CALL_PARAM("  vboTerrainPos_ = " << vboTerrainPos_);
        DEBUG_CALL_PARAM("  buffer size = " << vbPos << " bytes");
        DEBUG_CALL_PARAM("  data pointer = " << (void*)mesh.pos.data());
        return;
    }

    // Проверка размера загруженного буфера
    GLint bufferSize;
    gl_->glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    if (bufferSize != vbPos) {
        DEBUG_CALL_PARAM("WARNING: Position buffer size mismatch! Uploaded: " << vbPos << " Actual: " << bufferSize);
    }
    else {
        DEBUG_CALL_PARAM("Position buffer uploaded successfully: " << bufferSize << " bytes");
    }

    // ============== ЗАГРУЗКА ЦВЕТОВ ==============
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbCol, mesh.col.data(), usage);

    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("ERROR: OpenGL Error after color buffer: " << err);
        return;
    }

    gl_->glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    if (bufferSize != vbCol) {
        DEBUG_CALL_PARAM("WARNING: Color buffer size mismatch! Uploaded: " << vbCol << " Actual: " << bufferSize);
    }
    else {
        DEBUG_CALL_PARAM("Color buffer uploaded successfully: " << bufferSize << " bytes");
    }

    // ============== ЗАГРУЗКА НОРМАЛЕЙ ==============
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbNorm, mesh.norm.data(), usage);

    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("ERROR: OpenGL Error after normal buffer: " << err);
        return;
    }

    gl_->glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    if (bufferSize != vbNorm) {
        DEBUG_CALL_PARAM("WARNING: Normal buffer size mismatch! Uploaded: " << vbNorm << " Actual: " << bufferSize);
    }
    else {
        DEBUG_CALL_PARAM("Normal buffer uploaded successfully: " << bufferSize << " bytes");
    }

    // ============== ЗАГРУЗКА ИНДЕКСОВ ==============
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib, mesh.idx.data(), usage);

    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("ERROR: OpenGL Error after index buffer: " << err);
        return;
    }

    gl_->glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    if (bufferSize != ib) {
        DEBUG_CALL_PARAM("WARNING: Index buffer size mismatch! Uploaded: " << ib << " Actual: " << bufferSize);
    }
    else {
        DEBUG_CALL_PARAM("Index buffer uploaded successfully: " << bufferSize << " bytes");
        DEBUG_CALL_PARAM("  Number of indices: " << bufferSize / sizeof(uint32_t));
    }

    // ============== ПРОВЕРКА VAO НАСТРОЕК ==============
    gl_->glBindVertexArray(vaoTerrain_);

    // Проверка, что атрибуты включены
    GLint vertexAttribEnabled;
    gl_->glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vertexAttribEnabled);
    DEBUG_CALL_PARAM("Vertex attrib 0 (position) enabled: " << (vertexAttribEnabled ? "YES" : "NO"));

    gl_->glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vertexAttribEnabled);
    DEBUG_CALL_PARAM("Vertex attrib 1 (color) enabled: " << (vertexAttribEnabled ? "YES" : "NO"));

    gl_->glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vertexAttribEnabled);
    DEBUG_CALL_PARAM("Vertex attrib 2 (normal) enabled: " << (vertexAttribEnabled ? "YES" : "NO"));

    // Проверка элементного буфера в VAO
    GLint elementBuffer;
    gl_->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
    DEBUG_CALL_PARAM("Element buffer bound to VAO: " << elementBuffer << " (expected: " << iboTerrain_ << ")");

    gl_->glBindVertexArray(0);

    // ============== ОБНОВЛЕНИЕ СЧЕТЧИКОВ ==============
    terrainIndexCount_ = GLsizei(mesh.idx.size());
    DEBUG_CALL_PARAM("terrainIndexCount_ set to " << terrainIndexCount_);

    if (terrainRenderer_) {
        terrainRenderer_->updateIndexCount(terrainIndexCount_);
        DEBUG_CALL_PARAM("terrainRenderer indexCount updated to " << terrainIndexCount_);
    }

    // ============== ФИНАЛЬНАЯ ПРОВЕРКА ОШИБОК ==============
    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error at end of upload: " << err);
    }
    else {
        DEBUG_CALL_PARAM("No OpenGL errors at end of upload");
    }

    if (stats_) {
        stats_->updateMemoryStats(GLsizei(mesh.pos.size() / 3),
            terrainIndexCount_,
            terrainIndexCount_ / 3);
        stats_->stopGPUTimer();
    }

    DEBUG_CALL_PARAM("terrain upload complete - OK");
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
    DEBUG_CALL_PARAM("vertices=" << data.positions.size() / 3
        << " triangles=" << data.indices.size() / 3);

    if (data.positions.empty() || data.indices.empty()) {
        DEBUG_CALL_PARAM("WARNING: empty water data");
        waterIndexCount_ = 0;
        if (waterRenderer_) {
            waterRenderer_->updateIndexCount(0);
        }
        return;
    }

    // Очищаем предыдущие ошибки OpenGL
    while (gl_->glGetError() != GL_NO_ERROR) {}

    // Загрузка позиций
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER,
        data.positions.size() * sizeof(float),
        data.positions.data(),
        GL_STATIC_DRAW);

    GLenum err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after water position buffer: " << err);
        return;
    }

    // Загрузка edge flags
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterEdgeFlags_);
    gl_->glBufferData(GL_ARRAY_BUFFER,
        data.edgeFlags.size() * sizeof(float),
        data.edgeFlags.empty() ? nullptr : data.edgeFlags.data(),
        GL_STATIC_DRAW);

    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after water edge flags buffer: " << err);
        return;
    }

    // Загрузка индексов
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        data.indices.size() * sizeof(uint32_t),
        data.indices.data(),
        GL_STATIC_DRAW);

    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after water index buffer: " << err);
        return;
    }

    // Перенастройка атрибутов VAO (на всякий случай)
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
    DEBUG_CALL_PARAM("waterIndexCount_ set to " << waterIndexCount_);

    // Обновляем счетчик в рендерере
    if (waterRenderer_) {
        waterRenderer_->updateIndexCount(waterIndexCount_);
        DEBUG_CALL_PARAM("waterRenderer indexCount updated");
    }

    DEBUG_CALL_PARAM("water upload complete - OK");
}

bool HexSphereRenderer::checkShaderStatus(GLuint shader, const char* name) {
    GLint status;
    gl_->glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[1024];
        gl_->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        DEBUG_CALL_PARAM("Shader compilation error (" << name << "): " << log);
        return false;
    }
    return true;
}

bool HexSphereRenderer::checkProgramStatus(GLuint program, const char* name) {
    GLint status;
    gl_->glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        char log[1024];
        gl_->glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        DEBUG_CALL_PARAM("Program link error (" << name << "): " << log);
        return false;
    }
    return true;
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
    DEBUG_CALL_PARAM("terrainUsage=" << options.terrainUsage << "wireUsage=" << options.wireUsage);
    withContext([&]() {
        DEBUG_CALL_PARAM("context current");
        uploadWireInternal(scene.buildWireVertices(), options.wireUsage);
        DEBUG_CALL_PARAM("uploading terrain...");
        uploadTerrainInternal(scene.terrain(), options.terrainUsage);
        DEBUG_CALL_PARAM("uploading selection...");
        uploadSelectionOutlineInternal(scene.buildSelectionOutlineVertices());
        if (auto path = scene.buildPathPolyline()) {
            DEBUG_CALL_PARAM("uploading path...");
            uploadPathInternal(*path);
        } else {
            DEBUG_CALL_PARAM("no path to upload");
            uploadPathInternal({});
        }
        DEBUG_CALL_PARAM("uploading water...");
        uploadWaterInternal(scene.buildWaterGeometry());
    });
    DEBUG_CALL_PARAM("upload complete");
}

void HexSphereRenderer::renderScene(const RenderGraph& graph, const RenderCamera& camera, const SceneLighting& lighting) {
    if (!glReady_) {
        DEBUG_CALL_PARAM("SKIP - gl not ready");
        return;
    }

    // ============== ПРОВЕРКА 1: Валидность контекста ==============
    if (!gl_ || !owner_ || !owner_->context() || !owner_->context()->isValid()) {
        DEBUG_CALL_PARAM("ERROR: Invalid OpenGL context!");
        return;
    }

    // ============== ПРОВЕРКА 2: Валидность всех шейдерных программ ==============
    GLuint programs[] = { progWire_, progTerrain_, progSel_, progWater_, progModel_ };
    const char* names[] = { "Wire", "Terrain", "Sel", "Water", "Model" };
    bool needsRecreate = false;

    for (int i = 0; i < 5; i++) {
        if (programs[i] != 0 && !gl_->glIsProgram(programs[i])) {
            DEBUG_CALL_PARAM("WARNING: " << names[i] << " program " << programs[i] << " is invalid!");
            needsRecreate = true;
        }
    }

    // ============== ПРОВЕРКА 3: Пересоздание невалидных программ ==============
    if (needsRecreate) {
        DEBUG_CALL_PARAM("Recreating invalid shader programs...");

        // Сохраняем текущий контекст
        owner_->makeCurrent();

        // Пересоздаем программы
        if (progWire_ == 0 || !gl_->glIsProgram(progWire_)) {
            progWire_ = makeProgram(VS_WIRE, FS_WIRE);
            DEBUG_CALL_PARAM("  Recreated Wire program: " << progWire_);
        }

        if (progTerrain_ == 0 || !gl_->glIsProgram(progTerrain_)) {
            progTerrain_ = makeProgram(VS_TERRAIN, FS_TERRAIN);
            DEBUG_CALL_PARAM("  Recreated Terrain program: " << progTerrain_);

            // Обновляем uniform locations для terrain
            gl_->glUseProgram(progTerrain_);
            uMVP_Terrain_ = gl_->glGetUniformLocation(progTerrain_, "uMVP");
            uModel_ = gl_->glGetUniformLocation(progTerrain_, "uModel");
            uLightDir_ = gl_->glGetUniformLocation(progTerrain_, "uLightDir");
            DEBUG_CALL_PARAM("    uMVP_Terrain_=" << uMVP_Terrain_ << " uModel_=" << uModel_ << " uLightDir_=" << uLightDir_);
        }

        if (progSel_ == 0 || !gl_->glIsProgram(progSel_)) {
            progSel_ = makeProgram(VS_WIRE, FS_SEL);
            DEBUG_CALL_PARAM("  Recreated Selection program: " << progSel_);

            gl_->glUseProgram(progSel_);
            uMVP_Sel_ = gl_->glGetUniformLocation(progSel_, "uMVP");
        }

        if (progWater_ == 0 || !gl_->glIsProgram(progWater_)) {
            progWater_ = makeProgram(VS_WATER, FS_WATER);
            DEBUG_CALL_PARAM("  Recreated Water program: " << progWater_);

            gl_->glUseProgram(progWater_);
            uMVP_Water_ = gl_->glGetUniformLocation(progWater_, "uMVP");
            uTime_Water_ = gl_->glGetUniformLocation(progWater_, "uTime");
            uLightDir_Water_ = gl_->glGetUniformLocation(progWater_, "uLightDir");
            uViewPos_Water_ = gl_->glGetUniformLocation(progWater_, "uViewPos");
            uEnvMap_ = gl_->glGetUniformLocation(progWater_, "uEnvMap");
        }

        if (progModel_ == 0 || !gl_->glIsProgram(progModel_)) {
            progModel_ = makeProgram(VS_MODEL, FS_MODEL);
            DEBUG_CALL_PARAM("  Recreated Model program: " << progModel_);

            gl_->glUseProgram(progModel_);
            uMVP_Model_ = gl_->glGetUniformLocation(progModel_, "uMVP");
            uModel_Model_ = gl_->glGetUniformLocation(progModel_, "uModel");
            uLightDir_Model_ = gl_->glGetUniformLocation(progModel_, "uLightDir");
            uViewPos_Model_ = gl_->glGetUniformLocation(progModel_, "uViewPos");
            uColor_Model_ = gl_->glGetUniformLocation(progModel_, "uColor");
            uUseTexture_ = gl_->glGetUniformLocation(progModel_, "uUseTexture");
        }

        gl_->glUseProgram(0);

        // Обновляем рендереры с новыми программами
        if (terrainRenderer_) {
            terrainRenderer_->updateProgram(progTerrain_, uMVP_Terrain_, uModel_, uLightDir_);
        }
        if (waterRenderer_) {
            waterRenderer_->updateProgram(progWater_, uMVP_Water_, uTime_Water_, uLightDir_Water_, uViewPos_Water_, uEnvMap_);
        }
        if (entityRenderer_) {
            entityRenderer_->updatePrograms(progWire_, progSel_, progModel_, uMVP_Wire_, uMVP_Sel_, uMVP_Model_, uModel_Model_, uLightDir_Model_, uViewPos_Model_, uColor_Model_, uUseTexture_);
        }
        if (overlayRenderer_) {
            overlayRenderer_->updatePrograms(progWire_, progSel_, uMVP_Wire_, uMVP_Sel_);
        }

        DEBUG_CALL_PARAM("Shader programs recreated successfully");
    }

    // ============== НАСТРОЙКА VIEWPORT ==============
    const float dpr = owner_->devicePixelRatioF();
    int width = int(owner_->width() * dpr);
    int height = int(owner_->height() * dpr);

    if (width <= 0 || height <= 0) {
        DEBUG_CALL_PARAM("ERROR: Invalid viewport dimensions: " << width << "x" << height);
        return;
    }

    gl_->glViewport(0, 0, width, height);
    DEBUG_CALL_PARAM("Viewport set to " << width << "x" << height);

    // ============== ОЧИСТКА ЭКРАНА ==============
    gl_->glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ============== БАЗОВЫЕ НАСТРОЙКИ OPENGL ==============
    gl_->glDisable(GL_BLEND);
    gl_->glDepthMask(GL_TRUE);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glEnable(GL_CULL_FACE);
    gl_->glCullFace(GL_BACK);
    gl_->glFrontFace(GL_CCW);
    gl_->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Сбрасываем привязки
    gl_->glBindVertexArray(0);
    gl_->glUseProgram(0);

    // ============== ПРОВЕРКА ОШИБОК ПОСЛЕ НАСТРОЙКИ ==============
    GLenum err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after setup: " << err);
    }

    if (stats_) stats_->startGPUTimer();

    // ============== ПОДГОТОВКА КОНТЕКСТА РЕНДЕРА ==============
    QMatrix4x4 mvp = camera.projection * camera.view;
    QVector3D cameraPos = (camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

    RenderContext ctx{
        graph,
        camera,
        lighting,
        mvp,
        cameraPos
    };

    // ============== ОТЛАДКА ==============
    debugTerrainMeshSizes(ctx.graph.scene.terrain());

    // ============== РЕНДЕР ВСЕХ КОМПОНЕНТОВ ==============
    DEBUG_CALL_PARAM("Starting terrain render...");
    if (terrainRenderer_) {
        terrainRenderer_->render(ctx);
    }
    else {
        DEBUG_CALL_PARAM("ERROR: terrainRenderer_ is null!");
    }

    DEBUG_CALL_PARAM("Starting water render...");
    if (waterRenderer_) {
        waterRenderer_->render(ctx);
    }

    DEBUG_CALL_PARAM("Starting entities render...");
    if (entityRenderer_) {
        entityRenderer_->renderEntities(ctx);
    }

    DEBUG_CALL_PARAM("Starting overlay render...");
    if (overlayRenderer_) {
        overlayRenderer_->render(ctx);
    }

    DEBUG_CALL_PARAM("Starting trees render...");
    if (entityRenderer_) {
        entityRenderer_->renderTrees(ctx);
    }

    // ============== ФИНАЛЬНАЯ ПРОВЕРКА ОШИБОК ==============
    err = gl_->glGetError();
    if (err != GL_NO_ERROR) {
        DEBUG_CALL_PARAM("OpenGL Error after render: " << err);
    }
    else {
        DEBUG_CALL_PARAM("No OpenGL errors after render");
    }

    if (stats_) stats_->stopGPUTimer();

    DEBUG_CALL_PARAM("renderScene complete");
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

// ============== 2. Создание шейдеров ==============
bool HexSphereRenderer::createShaders() {
    progWire_ = makeProgram(VS_WIRE, FS_WIRE);
    progTerrain_ = makeProgram(VS_TERRAIN, FS_TERRAIN);
    progSel_ = makeProgram(VS_WIRE, FS_SEL);
    progWater_ = makeProgram(VS_WATER, FS_WATER);
    progModel_ = makeProgram(VS_MODEL, FS_MODEL);

    DEBUG_CALL_PARAM("progWire_=" << progWire_ << " progTerrain_=" << progTerrain_
        << " progSel_=" << progSel_ << " progWater_=" << progWater_
        << " progModel_=" << progModel_);

    // Проверка всех шейдеров
    logShaderInfo(progTerrain_, "Terrain");
    logShaderInfo(progWater_, "Water");
    logShaderInfo(progModel_, "Model");
    logShaderInfo(progWire_, "Wire");
    logShaderInfo(progSel_, "Selection");

    return (progWire_ && progTerrain_ && progSel_ && progWater_ && progModel_);
}

void HexSphereRenderer::logShaderInfo(GLuint program, const char* name) {
    if (program == 0) {
        DEBUG_CALL_PARAM(name << " shader program is 0!");
        return;
    }

    GLint status;
    gl_->glGetProgramiv(program, GL_LINK_STATUS, &status);
    DEBUG_CALL_PARAM(name << " shader link status: " << (status == GL_TRUE ? "OK" : "FAILED"));

    if (status == GL_TRUE) {
        GLint numUniforms;
        gl_->glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
        DEBUG_CALL_PARAM(name << " shader has " << numUniforms << " active uniforms");

        for (GLint i = 0; i < numUniforms; i++) {
            char nameBuf[256];
            GLsizei length;
            GLint size;
            GLenum type;
            gl_->glGetActiveUniform(program, i, sizeof(nameBuf), &length, &size, &type, nameBuf);
            GLint location = gl_->glGetUniformLocation(program, nameBuf);
            DEBUG_CALL_PARAM("  Uniform[" << i << "]: " << nameBuf << " at location " << location);
        }
    }
    else {
        char infoLog[1024];
        gl_->glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        DEBUG_CALL_PARAM(name << " shader info log: " << infoLog);
    }
}

// ============== 3. Uniform locations ==============
void HexSphereRenderer::setupUniformLocations() {
    // Wire shader
    gl_->glUseProgram(progWire_);
    uMVP_Wire_ = gl_->glGetUniformLocation(progWire_, "uMVP");
    DEBUG_CALL_PARAM("uMVP_Wire_=" << uMVP_Wire_);

    // Terrain shader
    gl_->glUseProgram(progTerrain_);
    uMVP_Terrain_ = gl_->glGetUniformLocation(progTerrain_, "uMVP");
    uModel_ = gl_->glGetUniformLocation(progTerrain_, "uModel");
    uLightDir_ = gl_->glGetUniformLocation(progTerrain_, "uLightDir");
    DEBUG_CALL_PARAM("uMVP_Terrain_=" << uMVP_Terrain_ << " uModel_=" << uModel_ << " uLightDir_=" << uLightDir_);

    // Selection shader
    gl_->glUseProgram(progSel_);
    uMVP_Sel_ = gl_->glGetUniformLocation(progSel_, "uMVP");
    DEBUG_CALL_PARAM("uMVP_Sel_=" << uMVP_Sel_);

    // Water shader
    gl_->glUseProgram(progWater_);
    uMVP_Water_ = gl_->glGetUniformLocation(progWater_, "uMVP");
    uTime_Water_ = gl_->glGetUniformLocation(progWater_, "uTime");
    uLightDir_Water_ = gl_->glGetUniformLocation(progWater_, "uLightDir");
    uViewPos_Water_ = gl_->glGetUniformLocation(progWater_, "uViewPos");
    uEnvMap_ = gl_->glGetUniformLocation(progWater_, "uEnvMap");
    DEBUG_CALL_PARAM("uMVP_Water_=" << uMVP_Water_ << " uTime_Water_=" << uTime_Water_
        << " uLightDir_Water_=" << uLightDir_Water_ << " uViewPos_Water_=" << uViewPos_Water_);

    // Model shader
    gl_->glUseProgram(progModel_);
    uMVP_Model_ = gl_->glGetUniformLocation(progModel_, "uMVP");
    uModel_Model_ = gl_->glGetUniformLocation(progModel_, "uModel");
    uLightDir_Model_ = gl_->glGetUniformLocation(progModel_, "uLightDir");
    uViewPos_Model_ = gl_->glGetUniformLocation(progModel_, "uViewPos");
    uColor_Model_ = gl_->glGetUniformLocation(progModel_, "uColor");
    uUseTexture_ = gl_->glGetUniformLocation(progModel_, "uUseTexture");
    DEBUG_CALL_PARAM("uMVP_Model_=" << uMVP_Model_ << " uModel_Model_=" << uModel_Model_);

    gl_->glUseProgram(0);
}

// ============== 5. Создание буферов ==============
void HexSphereRenderer::createBuffersAndVAOs() {
    gl_->glGenBuffers(1, &vboPositions_);
    gl_->glGenVertexArrays(1, &vaoWire_);

    gl_->glGenBuffers(1, &vboTerrainPos_);
    gl_->glGenBuffers(1, &vboTerrainCol_);
    gl_->glGenBuffers(1, &vboTerrainNorm_);
    gl_->glGenBuffers(1, &iboTerrain_);
    gl_->glGenVertexArrays(1, &vaoTerrain_);

    gl_->glGenBuffers(1, &vboSel_);
    gl_->glGenVertexArrays(1, &vaoSel_);

    gl_->glGenBuffers(1, &vboPath_);
    gl_->glGenVertexArrays(1, &vaoPath_);

    gl_->glGenBuffers(1, &vboWaterPos_);
    gl_->glGenBuffers(1, &iboWater_);
    gl_->glGenBuffers(1, &vboWaterEdgeFlags_);
    gl_->glGenVertexArrays(1, &vaoWater_);

    DEBUG_CALL_PARAM("buffers and VAOs created");
}

// ============== 6. Настройка VAO ==============
void HexSphereRenderer::setupWireVAO() {
    gl_->glBindVertexArray(vaoWire_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPositions_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);
    DEBUG_CALL_PARAM("wire VAO setup complete");
}

void HexSphereRenderer::setupTerrainVAO() {
    gl_->glBindVertexArray(vaoTerrain_);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(1);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(2);

    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    gl_->glBindVertexArray(0);
    DEBUG_CALL_PARAM("terrain VAO setup complete");
}

void HexSphereRenderer::setupSelectionVAO() {
    gl_->glBindVertexArray(vaoSel_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboSel_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);
    DEBUG_CALL_PARAM("selection VAO setup complete");
}

void HexSphereRenderer::setupPathVAO() {
    gl_->glBindVertexArray(vaoPath_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPath_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);
    DEBUG_CALL_PARAM("path VAO setup complete");
}

void HexSphereRenderer::setupWaterVAO() {
    gl_->glBindVertexArray(vaoWater_);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterPos_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboWaterEdgeFlags_);
    gl_->glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(1);

    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboWater_);
    gl_->glBindVertexArray(0);
    DEBUG_CALL_PARAM("water VAO setup complete");
}

// ============== 8. Загрузка модели дерева ==============
void HexSphereRenderer::loadTreeModel() {
    const QString modelPath = "Planet/tree.obj";
    treeModel_ = ModelHandler::loadShared(modelPath);
    if (treeModel_) {
        treeModel_->uploadToGPU();
        DEBUG_CALL_PARAM("tree model loaded successfully");
    }
    else {
        DEBUG_CALL_PARAM("WARNING: tree model not loaded");
    }
}

// ============== 9. Создание рендереров ==============
void HexSphereRenderer::createRenderers() {
    terrainRenderer_ = std::make_unique<TerrainRenderer>(
        gl_, progTerrain_, uMVP_Terrain_, uModel_, uLightDir_,
        vaoTerrain_, 0);

    waterRenderer_ = std::make_unique<WaterRenderer>(
        gl_, progWater_, uMVP_Water_, uTime_Water_, uLightDir_Water_,
        uViewPos_Water_, uEnvMap_, envCubemap_, vaoWater_, 0);

    entityRenderer_ = std::make_unique<EntityRenderer>(
        gl_, progWire_, progSel_, progModel_,
        uMVP_Wire_, uMVP_Sel_, uMVP_Model_, uModel_Model_,
        uLightDir_Model_, uViewPos_Model_, uColor_Model_, uUseTexture_,
        vaoPyramid_, pyramidVertexCount_, treeModel_);

    overlayRenderer_ = std::make_unique<OverlayRenderer>(
        gl_, progWire_, progSel_, uMVP_Wire_, uMVP_Sel_,
        vaoWire_, vaoSel_, vaoPath_,
        0, 0, 0);

    DEBUG_CALL_PARAM("all renderers created with zero counts");
}
