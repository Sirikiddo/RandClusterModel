#include "HexSphereRenderer.h"

#include <QOpenGLWidget>
#include <QtDebug>

#include "HexSphereWidget_shaders.h"
#include "SurfacePlacement.h"

namespace {

void orientToSurfaceNormal(QMatrix4x4& matrix, const QVector3D& normal) {
    QVector3D up = normal.normalized();

    QVector3D forward = (qAbs(QVector3D::dotProduct(up, QVector3D(0, 0, 1))) > 0.99f)
        ? QVector3D(1, 0, 0)
        : QVector3D(0, 0, 1);

    QVector3D right = QVector3D::crossProduct(forward, up).normalized();
    forward = QVector3D::crossProduct(up, right).normalized();

    QMatrix4x4 rotation;
    rotation.setColumn(0, QVector4D(right, 0.0f));
    rotation.setColumn(1, QVector4D(up, 0.0f));
    rotation.setColumn(2, QVector4D(forward, 0.0f));
    rotation.setColumn(3, QVector4D(0.0f, 0.0f, 0.0f, 1.0f));

    matrix = matrix * rotation;
}
} // namespace

struct HexSphereRenderer::RenderContext {
    const RenderGraph& graph;
    const RenderCamera& camera;
    const SceneLighting& lighting;
    QMatrix4x4 mvp;
    QVector3D cameraPos;
};

HexSphereRenderer::HexSphereRenderer(QOpenGLWidget* owner)
    : owner_(owner)
    , terrainSubsystem_(std::make_unique<TerrainSubsystem>(*this))
    , waterSubsystem_(std::make_unique<WaterSubsystem>(*this))
    , entitySubsystem_(std::make_unique<EntitySubsystem>(*this))
    , overlaySubsystem_(std::make_unique<OverlaySubsystem>(*this)) {}

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
    gl_->glGenVertexArrays(1, &vaoTerrain_);
    gl_->glGenBuffers(1, &vboSel_);
    gl_->glGenVertexArrays(1, &vaoSel_);
    gl_->glGenBuffers(1, &vboPath_);
    gl_->glGenVertexArrays(1, &vaoPath_);
    gl_->glGenBuffers(1, &vboWaterPos_);
    gl_->glGenBuffers(1, &iboWater_);
    gl_->glGenBuffers(1, &vboWaterEdgeFlags_);
    gl_->glGenVertexArrays(1, &vaoWater_);

    gl_->glBindVertexArray(vaoWire_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPositions_);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glBindVertexArray(0);

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
    if (stats_) stats_->startGPUTimer();

    const GLsizeiptr vbPos = GLsizeiptr(mesh.pos.size() * sizeof(float));
    const GLsizeiptr vbCol = GLsizeiptr(mesh.col.size() * sizeof(float));
    const GLsizeiptr vbNorm = GLsizeiptr(mesh.norm.size() * sizeof(float));
    const GLsizeiptr ib = GLsizeiptr(mesh.idx.size() * sizeof(uint32_t));

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbPos, mesh.pos.empty() ? nullptr : mesh.pos.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainCol_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbCol, mesh.col.empty() ? nullptr : mesh.col.data(), usage);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboTerrainNorm_);
    gl_->glBufferData(GL_ARRAY_BUFFER, vbNorm, mesh.norm.empty() ? nullptr : mesh.norm.data(), usage);

    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iboTerrain_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib, mesh.idx.empty() ? nullptr : mesh.idx.data(), usage);

    terrainIndexCount_ = GLsizei(mesh.idx.size());

    if (stats_) {
        stats_->updateMemoryStats(GLsizei(mesh.pos.size() / 3), terrainIndexCount_, terrainIndexCount_ / 3);
        stats_->stopGPUTimer();
    }
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

struct HexSphereRenderer::TerrainSubsystem {
    explicit TerrainSubsystem(HexSphereRenderer& renderer) : renderer_(renderer) {}

    void render(const RenderContext& ctx) const {
        if (renderer_.terrainIndexCount_ == 0 || renderer_.progTerrain_ == 0) return;

        auto* gl = renderer_.gl_;
        gl->glUseProgram(renderer_.progTerrain_);
        gl->glUniformMatrix4fv(renderer_.uMVP_Terrain_, 1, GL_FALSE, ctx.mvp.constData());
        QMatrix4x4 model; model.setToIdentity();
        gl->glUniformMatrix4fv(renderer_.uModel_, 1, GL_FALSE, model.constData());
        const QVector3D& lightDir = ctx.lighting.direction;
        gl->glUniform3f(renderer_.uLightDir_, lightDir.x(), lightDir.y(), lightDir.z());
        gl->glBindVertexArray(renderer_.vaoTerrain_);
        gl->glDrawElements(GL_TRIANGLES, renderer_.terrainIndexCount_, GL_UNSIGNED_INT, nullptr);
        gl->glBindVertexArray(0);
    }

private:
    HexSphereRenderer& renderer_;
};

struct HexSphereRenderer::WaterSubsystem {
    explicit WaterSubsystem(HexSphereRenderer& renderer) : renderer_(renderer) {}

    void render(const RenderContext& ctx) const {
        if (renderer_.waterIndexCount_ == 0 || renderer_.progWater_ == 0) return;

        auto* gl = renderer_.gl_;
        gl->glUseProgram(renderer_.progWater_);
        gl->glUniformMatrix4fv(renderer_.uMVP_Water_, 1, GL_FALSE, ctx.mvp.constData());
        gl->glUniform1f(renderer_.uTime_Water_, ctx.lighting.waterTime);
        const QVector3D& lightDir = ctx.lighting.direction;
        gl->glUniform3f(renderer_.uLightDir_Water_, lightDir.x(), lightDir.y(), lightDir.z());
        gl->glUniform3f(renderer_.uViewPos_Water_, ctx.cameraPos.x(), ctx.cameraPos.y(), ctx.cameraPos.z());
        if (renderer_.uEnvMap_ != -1 && renderer_.envCubemap_ != 0) {
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_CUBE_MAP, renderer_.envCubemap_);
            gl->glUniform1i(renderer_.uEnvMap_, 0);
        }

        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->glDepthMask(GL_FALSE);
        gl->glBindVertexArray(renderer_.vaoWater_);
        gl->glDrawElements(GL_TRIANGLES, renderer_.waterIndexCount_, GL_UNSIGNED_INT, nullptr);
        gl->glBindVertexArray(0);
        gl->glDepthMask(GL_TRUE);
        gl->glDisable(GL_BLEND);
    }

private:
    HexSphereRenderer& renderer_;
};

struct HexSphereRenderer::EntitySubsystem {
    explicit EntitySubsystem(HexSphereRenderer& renderer) : renderer_(renderer) {}

    void renderEntities(const RenderContext& ctx) const {
        const QMatrix4x4& view = ctx.camera.view;
        const QMatrix4x4& proj = ctx.camera.projection;
        const QMatrix4x4 vp = proj * view;
        for (const auto& e : ctx.graph.sceneGraph.entities()) {
            QVector3D surfacePos = computeSurfacePoint(ctx.graph.scene, e->currentCell(), ctx.graph.heightStep);

            QMatrix4x4 model;
            model.translate(surfacePos);
            QVector3D surfaceNormal = surfacePos.normalized();
            orientToSurfaceNormal(model, surfaceNormal);
            model.scale(0.08f);
            if (e->selected()) {
                model.scale(1.2f);
            }

            const QMatrix4x4 entityMvp = vp * model;
            if (e->selected()) {
                renderer_.gl_->glUseProgram(renderer_.progSel_);
                renderer_.gl_->glUniformMatrix4fv(renderer_.uMVP_Sel_, 1, GL_FALSE, entityMvp.constData());
            } else {
                renderer_.gl_->glUseProgram(renderer_.progWire_);
                renderer_.gl_->glUniformMatrix4fv(renderer_.uMVP_Wire_, 1, GL_FALSE, entityMvp.constData());
            }
            renderer_.gl_->glBindVertexArray(renderer_.vaoPyramid_);
            renderer_.gl_->glDrawArrays(GL_TRIANGLES, 0, renderer_.pyramidVertexCount_);
        }
    }

    void renderTrees(const RenderContext& ctx) const {
        if (!renderer_.treeModel_ || !renderer_.treeModel_->isInitialized() || renderer_.progModel_ == 0 || renderer_.treeModel_->isEmpty()) return;

        renderer_.gl_->glUseProgram(renderer_.progModel_);

        QVector3D globalLightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
        QVector3D eye = (ctx.camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();

        renderer_.gl_->glUniform3f(renderer_.uLightDir_Model_, globalLightDir.x(), globalLightDir.y(), globalLightDir.z());
        renderer_.gl_->glUniform3f(renderer_.uViewPos_Model_, eye.x(), eye.y(), eye.z());
        renderer_.gl_->glUniform3f(renderer_.uColor_Model_, 0.15f, 0.5f, 0.1f);
        renderer_.gl_->glUniform1i(renderer_.uUseTexture_, renderer_.treeModel_->hasUVs() ? 1 : 0);

        const auto& cells = ctx.graph.scene.model().cells();
        int treesRendered = 0;
        const int maxTrees = 25;

        for (size_t i = 0; i < cells.size() && treesRendered < maxTrees; ++i) {
            if (cells[i].biome == Biome::Grass && (i % 3 == 0)) {
                QVector3D treePos = computeSurfacePoint(ctx.graph.scene, static_cast<int>(i), ctx.graph.heightStep);

                QMatrix4x4 model;
                model.translate(treePos);
                orientToSurfaceNormal(model, treePos.normalized());
                model.scale(0.05f + 0.02f * (i % 5));

                QMatrix4x4 mvpTree = ctx.camera.projection * ctx.camera.view * model;
                renderer_.gl_->glUniformMatrix4fv(renderer_.uMVP_Model_, 1, GL_FALSE, mvpTree.constData());
                renderer_.gl_->glUniformMatrix4fv(renderer_.uModel_Model_, 1, GL_FALSE, model.constData());

                renderer_.treeModel_->draw(renderer_.progModel_, mvpTree, model, ctx.camera.view);
                ++treesRendered;
            }
        }
    }

private:
    HexSphereRenderer& renderer_;
};

struct HexSphereRenderer::OverlaySubsystem {
    explicit OverlaySubsystem(HexSphereRenderer& renderer) : renderer_(renderer) {}

    void render(const RenderContext& ctx) const {
        if (renderer_.selLineVertexCount_ > 0 && renderer_.progSel_) {
            renderer_.gl_->glUseProgram(renderer_.progSel_);
            renderer_.gl_->glUniformMatrix4fv(renderer_.uMVP_Sel_, 1, GL_FALSE, ctx.mvp.constData());
            renderer_.gl_->glBindVertexArray(renderer_.vaoSel_);
            renderer_.gl_->glDrawArrays(GL_LINES, 0, renderer_.selLineVertexCount_);
        }
        if (renderer_.lineVertexCount_ > 0 && renderer_.progWire_) {
            renderer_.gl_->glUseProgram(renderer_.progWire_);
            renderer_.gl_->glUniformMatrix4fv(renderer_.uMVP_Wire_, 1, GL_FALSE, ctx.mvp.constData());
            renderer_.gl_->glBindVertexArray(renderer_.vaoWire_);
            renderer_.gl_->glDrawArrays(GL_LINES, 0, renderer_.lineVertexCount_);
        }
        if (renderer_.pathVertexCount_ > 0 && renderer_.progWire_) {
            renderer_.gl_->glUseProgram(renderer_.progWire_);
            renderer_.gl_->glUniformMatrix4fv(renderer_.uMVP_Wire_, 1, GL_FALSE, ctx.mvp.constData());
            renderer_.gl_->glBindVertexArray(renderer_.vaoPath_);
            renderer_.gl_->glDrawArrays(GL_LINE_STRIP, 0, renderer_.pathVertexCount_);
        }
    }

private:
    HexSphereRenderer& renderer_;
};

void HexSphereRenderer::renderScene(const RenderGraph& graph, const RenderCamera& camera, const SceneLighting& lighting) {
    if (!glReady_) return;

    const float dpr = owner_->devicePixelRatioF();
    gl_->glViewport(0, 0, int(owner_->width() * dpr), int(owner_->height() * dpr));
    gl_->glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (stats_) stats_->startGPUTimer();

    RenderContext ctx{graph, camera, lighting, camera.projection * camera.view,
                      (camera.view.inverted() * QVector4D(0, 0, 0, 1)).toVector3D()};

    terrainSubsystem_->render(ctx);
    waterSubsystem_->render(ctx);
    entitySubsystem_->renderEntities(ctx);
    overlaySubsystem_->render(ctx);
    entitySubsystem_->renderTrees(ctx);

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
