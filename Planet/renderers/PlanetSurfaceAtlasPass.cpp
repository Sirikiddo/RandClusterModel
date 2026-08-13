#include "renderers/PlanetSurfaceAtlasPass.h"

#include <QByteArray>
#include <QtDebug>

#include <algorithm>
#include <array>
#include <cmath>

#include "controllers/HexSphereSceneController.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct ShoreArc {
    QVector3D a;
    QVector3D b;
    QVector3D normal;
    float length = 0.0f;
};

constexpr const char* kAtlasVs = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aSurfaceKind;
layout(location=2) in float aShoreDistance;

uniform mat4 uViewProjection;

out vec3 vWorldPos;
flat out float vSurfaceKind;
out float vShoreDistance;

void main() {
    vWorldPos = aPos;
    vSurfaceKind = aSurfaceKind;
    vShoreDistance = aShoreDistance;
    gl_Position = uViewProjection * vec4(aPos, 1.0);
}
)GLSL";

constexpr const char* kAtlasFs = R"GLSL(
#version 330 core
in vec3 vWorldPos;
flat in float vSurfaceKind;
in float vShoreDistance;

layout(location=0) out float outRadius;
layout(location=1) out float outSurfaceKind;
layout(location=2) out float outShoreDistance;

void main() {
    outRadius = length(vWorldPos);
    outSurfaceKind = vSurfaceKind;
    outShoreDistance = vShoreDistance;
}
)GLSL";

QVector3D loadVec3(const std::vector<float>& data, uint32_t index) {
    const size_t base = static_cast<size_t>(index) * 3u;
    return QVector3D(data[base], data[base + 1u], data[base + 2u]);
}

float classifySurfaceKind(const HexSphereModel& model, int cellId) {
    if (cellId < 0 || cellId >= static_cast<int>(model.cells().size())) {
        return 0.0f;
    }
    return model.cells()[static_cast<size_t>(cellId)].biome == Biome::Sea ? 3.0f : 1.0f;
}

float clampedAcos(float value) {
    return std::acos(std::clamp(value, -1.0f, 1.0f));
}

float angularDistanceToArc(const QVector3D& direction, const ShoreArc& arc) {
    const QVector3D p = direction.normalized();
    QVector3D projected = p - arc.normal * QVector3D::dotProduct(p, arc.normal);
    if (!projected.isNull()) {
        projected.normalize();
        if (QVector3D::dotProduct(projected, p) < 0.0f) {
            projected = -projected;
        }
        const float aToProjection = clampedAcos(QVector3D::dotProduct(arc.a, projected));
        const float projectionToB = clampedAcos(QVector3D::dotProduct(projected, arc.b));
        if (aToProjection + projectionToB <= arc.length + 2e-4f) {
            return clampedAcos(QVector3D::dotProduct(p, projected));
        }
    }
    return std::min(
        clampedAcos(QVector3D::dotProduct(p, arc.a)),
        clampedAcos(QVector3D::dotProduct(p, arc.b)));
}

std::vector<ShoreArc> buildShoreArcs(const HexSphereModel& model) {
    std::vector<ShoreArc> arcs;
    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();
    for (const Cell& cell : cells) {
        const size_t edgeCount = std::min(cell.poly.size(), cell.neighbors.size());
        for (size_t edge = 0; edge < edgeCount; ++edge) {
            const int neighborId = cell.neighbors[edge];
            if (neighborId < 0 || neighborId >= static_cast<int>(cells.size()) || cell.id > neighborId) {
                continue;
            }
            if ((cell.biome == Biome::Sea) == (cells[static_cast<size_t>(neighborId)].biome == Biome::Sea)) {
                continue;
            }
            ShoreArc arc;
            arc.a = dual[static_cast<size_t>(cell.poly[edge])].normalized();
            arc.b = dual[static_cast<size_t>(cell.poly[(edge + 1u) % cell.poly.size()])].normalized();
            arc.normal = QVector3D::crossProduct(arc.a, arc.b).normalized();
            arc.length = clampedAcos(QVector3D::dotProduct(arc.a, arc.b));
            if (!arc.normal.isNull() && arc.length > 1e-6f) {
                arcs.push_back(arc);
            }
        }
    }
    return arcs;
}

} // namespace

void PlanetSurfaceAtlasPass::initialize(QOpenGLFunctions_3_3_Core* gl) {
    gl_ = gl;
    ensureResources();
}

void PlanetSurfaceAtlasPass::release() {
    if (!gl_) {
        return;
    }
    if (program_ != 0) {
        gl_->glDeleteProgram(program_);
        program_ = 0;
    }
    if (vao_ != 0) {
        gl_->glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (vboPos_ != 0) {
        gl_->glDeleteBuffers(1, &vboPos_);
        vboPos_ = 0;
    }
    if (vboKind_ != 0) {
        gl_->glDeleteBuffers(1, &vboKind_);
        vboKind_ = 0;
    }
    if (vboShoreDistance_ != 0) {
        gl_->glDeleteBuffers(1, &vboShoreDistance_);
        vboShoreDistance_ = 0;
    }
    if (depthRbo_ != 0) {
        gl_->glDeleteRenderbuffers(1, &depthRbo_);
        depthRbo_ = 0;
    }
    if (fbo_ != 0) {
        gl_->glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (radiusCubemap_ != 0) {
        gl_->glDeleteTextures(1, &radiusCubemap_);
        radiusCubemap_ = 0;
    }
    if (kindCubemap_ != 0) {
        gl_->glDeleteTextures(1, &kindCubemap_);
        kindCubemap_ = 0;
    }
    if (shoreDistanceCubemap_ != 0) {
        gl_->glDeleteTextures(1, &shoreDistanceCubemap_);
        shoreDistanceCubemap_ = 0;
    }
    vertexCount_ = 0;
    dirty_ = false;
}

bool PlanetSurfaceAtlasPass::ready() const {
    return gl_ != nullptr
        && !initializationFailed_
        && program_ != 0
        && vao_ != 0
        && vboPos_ != 0
        && vboKind_ != 0
        && vboShoreDistance_ != 0
        && fbo_ != 0
        && depthRbo_ != 0
        && radiusCubemap_ != 0
        && kindCubemap_ != 0
        && shoreDistanceCubemap_ != 0;
}

void PlanetSurfaceAtlasPass::ensureResources() {
    if (!gl_ || initializationFailed_) {
        return;
    }
    if (program_ == 0) {
        program_ = makeProgram(kAtlasVs, kAtlasFs);
        if (program_ == 0) {
            initializationFailed_ = true;
            return;
        }
        uViewProjection_ = gl_->glGetUniformLocation(program_, "uViewProjection");
    }
    if (vao_ == 0) {
        gl_->glGenVertexArrays(1, &vao_);
        gl_->glGenBuffers(1, &vboPos_);
        gl_->glGenBuffers(1, &vboKind_);
        gl_->glGenBuffers(1, &vboShoreDistance_);

        gl_->glBindVertexArray(vao_);

        gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPos_);
        gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        gl_->glEnableVertexAttribArray(0);

        gl_->glBindBuffer(GL_ARRAY_BUFFER, vboKind_);
        gl_->glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
        gl_->glEnableVertexAttribArray(1);

        gl_->glBindBuffer(GL_ARRAY_BUFFER, vboShoreDistance_);
        gl_->glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
        gl_->glEnableVertexAttribArray(2);

        gl_->glBindVertexArray(0);
    }

    auto initCube = [&](GLuint& textureId, GLint filter) {
        if (textureId != 0) {
            return;
        }
        gl_->glGenTextures(1, &textureId);
        gl_->glBindTexture(GL_TEXTURE_CUBE_MAP, textureId);
        for (int face = 0; face < 6; ++face) {
            gl_->glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0,
                GL_R32F,
                resolution_,
                resolution_,
                0,
                GL_RED,
                GL_FLOAT,
                nullptr);
        }
        gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, filter);
        gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, filter);
        gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    };

    initCube(radiusCubemap_, GL_LINEAR);
    initCube(kindCubemap_, GL_NEAREST);
    initCube(shoreDistanceCubemap_, GL_LINEAR);
    gl_->glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    if (depthRbo_ == 0) {
        gl_->glGenRenderbuffers(1, &depthRbo_);
        gl_->glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
        gl_->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, resolution_, resolution_);
    }

    if (fbo_ == 0) {
        gl_->glGenFramebuffers(1, &fbo_);
        gl_->glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        gl_->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);
        gl_->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

GLuint PlanetSurfaceAtlasPass::makeProgram(const char* vs, const char* fs) const {
    auto compile = [&](GLenum stage, const char* source) -> GLuint {
        const GLuint shader = gl_->glCreateShader(stage);
        gl_->glShaderSource(shader, 1, &source, nullptr);
        gl_->glCompileShader(shader);
        GLint success = GL_FALSE;
        gl_->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success == GL_TRUE) return shader;
        GLint logLength = 0;
        gl_->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log(std::max(logLength, 1), '\0');
        gl_->glGetShaderInfoLog(shader, log.size(), nullptr, log.data());
        qCritical().noquote() << "Surface atlas shader compilation failed:" << log;
        gl_->glDeleteShader(shader);
        return 0;
    };

    const GLuint vertexShader = compile(GL_VERTEX_SHADER, vs);
    const GLuint fragmentShader = compile(GL_FRAGMENT_SHADER, fs);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) gl_->glDeleteShader(vertexShader);
        if (fragmentShader != 0) gl_->glDeleteShader(fragmentShader);
        return 0;
    }

    GLuint program = gl_->glCreateProgram();
    gl_->glAttachShader(program, vertexShader);
    gl_->glAttachShader(program, fragmentShader);
    gl_->glLinkProgram(program);

    GLint success = GL_FALSE;
    gl_->glGetProgramiv(program, GL_LINK_STATUS, &success);

    gl_->glDeleteShader(vertexShader);
    gl_->glDeleteShader(fragmentShader);
    if (success != GL_TRUE) {
        GLint logLength = 0;
        gl_->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        QByteArray log(std::max(logLength, 1), '\0');
        gl_->glGetProgramInfoLog(program, log.size(), nullptr, log.data());
        qCritical().noquote() << "Surface atlas shader link failed:" << log;
        gl_->glDeleteProgram(program);
        return 0;
    }
    return program;
}

QMatrix4x4 PlanetSurfaceAtlasPass::cubeFaceView(int faceIndex) const {
    static const std::array<QVector3D, 6> kTargets = {
        QVector3D(1.0f, 0.0f, 0.0f),
        QVector3D(-1.0f, 0.0f, 0.0f),
        QVector3D(0.0f, 1.0f, 0.0f),
        QVector3D(0.0f, -1.0f, 0.0f),
        QVector3D(0.0f, 0.0f, 1.0f),
        QVector3D(0.0f, 0.0f, -1.0f),
    };
    static const std::array<QVector3D, 6> kUps = {
        QVector3D(0.0f, -1.0f, 0.0f),
        QVector3D(0.0f, -1.0f, 0.0f),
        QVector3D(0.0f, 0.0f, 1.0f),
        QVector3D(0.0f, 0.0f, -1.0f),
        QVector3D(0.0f, -1.0f, 0.0f),
        QVector3D(0.0f, -1.0f, 0.0f),
    };

    QMatrix4x4 view;
    view.lookAt(QVector3D(0.0f, 0.0f, 0.0f), kTargets[static_cast<size_t>(faceIndex)], kUps[static_cast<size_t>(faceIndex)]);
    return view;
}

void PlanetSurfaceAtlasPass::updateMesh(const TerrainMesh& mesh, const HexSphereModel& model) {
    ensureResources();
    if (!ready()) {
        return;
    }

    std::vector<float> positions;
    std::vector<float> kinds;
    std::vector<float> shoreDistances;
    positions.reserve(mesh.idx.size() * 3u);
    kinds.reserve(mesh.idx.size());
    shoreDistances.reserve(mesh.idx.size());

    stats_ = {};
    const std::vector<ShoreArc> shoreline = buildShoreArcs(model);
    stats_.shorelineArcCount = static_cast<int>(shoreline.size());
    const float waterRadius = model.waterSurfaceRadius();
    for (const Cell& cell : model.cells()) {
        if (cell.biome == Biome::Sea) {
            stats_.maximumSeaDepth = std::max(
                stats_.maximumSeaDepth,
                waterRadius - model.radiusForHeight(static_cast<float>(cell.height)));
        }
    }
    const size_t triangleCount = mesh.idx.size() / 3u;
    for (size_t tri = 0; tri < triangleCount; ++tri) {
        const uint32_t ia = mesh.idx[tri * 3u + 0u];
        const uint32_t ib = mesh.idx[tri * 3u + 1u];
        const uint32_t ic = mesh.idx[tri * 3u + 2u];
        const QVector3D a = loadVec3(mesh.pos, ia);
        const QVector3D b = loadVec3(mesh.pos, ib);
        const QVector3D c = loadVec3(mesh.pos, ic);
        const TriangleSurfaceRole role = tri < mesh.triSurfaceRole.size()
            ? mesh.triSurfaceRole[tri]
            : TriangleSurfaceRole::Cliff;
        if (role != TriangleSurfaceRole::Top) {
            ++stats_.culledTriangles;
            continue;
        }

        const int owner = tri < mesh.triOwner.size() ? mesh.triOwner[tri] : -1;
        const float surfaceKind = classifySurfaceKind(model, owner);
        const float shoreSign = surfaceKind == 3.0f ? 1.0f : -1.0f;

        auto appendVertex = [&](const QVector3D& p) {
            positions.push_back(p.x());
            positions.push_back(p.y());
            positions.push_back(p.z());
            kinds.push_back(surfaceKind);
            float angularDistance = kPi;
            for (const ShoreArc& arc : shoreline) {
                angularDistance = std::min(angularDistance, angularDistanceToArc(p, arc));
            }
            shoreDistances.push_back(shoreSign * angularDistance * waterRadius);
        };

        appendVertex(a);
        appendVertex(b);
        appendVertex(c);
        ++stats_.keptTriangles;
    }

    vertexCount_ = static_cast<GLsizei>(positions.size() / 3u);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(float), positions.empty() ? nullptr : positions.data(), GL_STATIC_DRAW);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboKind_);
    gl_->glBufferData(GL_ARRAY_BUFFER, kinds.size() * sizeof(float), kinds.empty() ? nullptr : kinds.data(), GL_STATIC_DRAW);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboShoreDistance_);
    gl_->glBufferData(GL_ARRAY_BUFFER, shoreDistances.size() * sizeof(float), shoreDistances.empty() ? nullptr : shoreDistances.data(), GL_STATIC_DRAW);

    dirty_ = true;
}

void PlanetSurfaceAtlasPass::renderIfDirty() {
    ensureResources();
    if (!dirty_ || !ready()) {
        return;
    }

    GLint previousFbo = 0;
    GLint previousViewport[4] = { 0, 0, 0, 0 };
    GLboolean blendEnabled = gl_->glIsEnabled(GL_BLEND);
    GLboolean cullEnabled = gl_->glIsEnabled(GL_CULL_FACE);
    GLboolean depthEnabled = gl_->glIsEnabled(GL_DEPTH_TEST);
    GLboolean previousDepthMask = GL_TRUE;
    GLint previousDepthFunc = GL_LESS;
    GLfloat previousClearDepth = 1.0f;
    GLint previousProgram = 0;
    GLint previousVao = 0;
    gl_->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    gl_->glGetIntegerv(GL_VIEWPORT, previousViewport);
    gl_->glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    gl_->glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    gl_->glGetFloatv(GL_DEPTH_CLEAR_VALUE, &previousClearDepth);
    gl_->glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    gl_->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);

    QMatrix4x4 projection;
    projection.perspective(90.0f, 1.0f, 0.001f, 4.0f);

    gl_->glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl_->glViewport(0, 0, resolution_, resolution_);
    gl_->glDisable(GL_BLEND);
    gl_->glDisable(GL_CULL_FACE);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glDepthMask(GL_TRUE);
    gl_->glDepthFunc(GL_GREATER);
    gl_->glClearDepth(0.0);
    gl_->glUseProgram(program_);
    gl_->glBindVertexArray(vao_);

    const GLenum drawBuffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    for (int face = 0; face < 6; ++face) {
        gl_->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, radiusCubemap_, 0);
        gl_->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, kindCubemap_, 0);
        gl_->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, shoreDistanceCubemap_, 0);
        gl_->glDrawBuffers(3, drawBuffers);
        gl_->glClearBufferfv(GL_COLOR, 0, std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }.data());
        gl_->glClearBufferfv(GL_COLOR, 1, std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }.data());
        gl_->glClearBufferfv(GL_COLOR, 2, std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }.data());
        gl_->glClear(GL_DEPTH_BUFFER_BIT);

        const QMatrix4x4 viewProjection = projection * cubeFaceView(face);
        gl_->glUniformMatrix4fv(uViewProjection_, 1, GL_FALSE, viewProjection.constData());
        if (vertexCount_ > 0) {
            gl_->glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
        }
    }

    gl_->glBindVertexArray(static_cast<GLuint>(previousVao));
    gl_->glUseProgram(static_cast<GLuint>(previousProgram));
    gl_->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    gl_->glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    if (blendEnabled) {
        gl_->glEnable(GL_BLEND);
    } else {
        gl_->glDisable(GL_BLEND);
    }
    if (cullEnabled) {
        gl_->glEnable(GL_CULL_FACE);
    } else {
        gl_->glDisable(GL_CULL_FACE);
    }
    if (depthEnabled) {
        gl_->glEnable(GL_DEPTH_TEST);
    } else {
        gl_->glDisable(GL_DEPTH_TEST);
    }
    gl_->glDepthMask(previousDepthMask);
    gl_->glDepthFunc(static_cast<GLenum>(previousDepthFunc));
    gl_->glClearDepth(previousClearDepth);

    dirty_ = false;
}
