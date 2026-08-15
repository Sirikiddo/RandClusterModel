#include "renderers/PlanetSurfaceAtlasPass.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QtDebug>

#include <array>

#include "controllers/HexSphereSceneController.h"
#include "renderers/SurfaceAtlasMeshBuilder.h"

namespace {

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

    QElapsedTimer totalTimer;
    totalTimer.start();
    const size_t triangleCount = mesh.idx.size() / 3u;
    SurfaceAtlasMeshData atlas = SurfaceAtlasMeshBuilder::build(mesh, model);
    stats_ = atlas.stats;
    vertexCount_ = static_cast<GLsizei>(atlas.positions.size() / 3u);

    QElapsedTimer stageTimer;
    stageTimer.start();
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboPos_);
    gl_->glBufferData(GL_ARRAY_BUFFER, atlas.positions.size() * sizeof(float), atlas.positions.empty() ? nullptr : atlas.positions.data(), GL_STATIC_DRAW);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboKind_);
    gl_->glBufferData(GL_ARRAY_BUFFER, atlas.kinds.size() * sizeof(float), atlas.kinds.empty() ? nullptr : atlas.kinds.data(), GL_STATIC_DRAW);

    gl_->glBindBuffer(GL_ARRAY_BUFFER, vboShoreDistance_);
    gl_->glBufferData(GL_ARRAY_BUFFER, atlas.shoreDistances.size() * sizeof(float), atlas.shoreDistances.empty() ? nullptr : atlas.shoreDistances.data(), GL_STATIC_DRAW);
    const double gpuUploadMs = stageTimer.nsecsElapsed() / 1000000.0;

    dirty_ = true;
    qInfo().nospace()
        << "[Perf][Generation] stage=surface_atlas_update"
        << " input_triangles=" << triangleCount
        << " kept_triangles=" << stats_.keptTriangles
        << " culled_triangles=" << stats_.culledTriangles
        << " shoreline_arcs=" << stats_.shorelineArcCount
        << " atlas_vertices=" << stats_.atlasVertices
        << " unique_directions=" << stats_.uniqueDirections
        << " cache_hits=" << stats_.cacheHits
        << " bvh_nodes=" << stats_.bvhNodes
        << " bvh_node_visits=" << stats_.bvhNodeVisits
        << " candidate_arcs=" << stats_.candidateArcs
        << " exact_distance_tests=" << stats_.exactDistanceTests
        << " distance_tests=" << stats_.exactDistanceTests
        << " brute_force_tests=" << stats_.bruteForceTests
        << " workers=" << stats_.workers
        << " shoreline_ms=" << stats_.shorelineMs
        << " index_build_ms=" << stats_.indexBuildMs
        << " distance_query_ms=" << stats_.distanceQueryMs
        << " distance_field_ms=" << stats_.distanceQueryMs
        << " gpu_upload_ms=" << gpuUploadMs
        << " total_ms=" << totalTimer.nsecsElapsed() / 1000000.0;
}

void PlanetSurfaceAtlasPass::renderIfDirty() {
    ensureResources();
    if (!dirty_ || !ready()) {
        return;
    }

    QElapsedTimer timer;
    timer.start();
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
    qInfo().nospace()
        << "[Perf][Generation] stage=surface_atlas_render_submit"
        << " vertices=" << vertexCount_
        << " cubemap_faces=6"
        << " cpu_submit_ms=" << timer.nsecsElapsed() / 1000000.0;
}
