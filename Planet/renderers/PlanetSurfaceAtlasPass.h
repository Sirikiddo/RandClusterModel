#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QVector3D>

#include <cstdint>
#include <vector>

#include "renderers/TerrainTessellator.h"

class HexSphereModel;

class PlanetSurfaceAtlasPass {
public:
    struct BuildStats {
        int keptTriangles = 0;
        int culledTriangles = 0;
        int shorelineArcCount = 0;
        float maximumSeaDepth = 0.0f;
    };

    PlanetSurfaceAtlasPass() = default;
    ~PlanetSurfaceAtlasPass() = default;

    void initialize(QOpenGLFunctions_3_3_Core* gl);
    void release();

    void updateMesh(const TerrainMesh& mesh, const HexSphereModel& model);
    void renderIfDirty();

    GLuint radiusTexture() const { return radiusCubemap_; }
    GLuint kindTexture() const { return kindCubemap_; }
    GLuint shoreDistanceTexture() const { return shoreDistanceCubemap_; }
    int resolution() const { return resolution_; }
    const BuildStats& stats() const { return stats_; }
    bool ready() const;

private:
    void ensureResources();
    GLuint makeProgram(const char* vs, const char* fs) const;
    QMatrix4x4 cubeFaceView(int faceIndex) const;

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vboPos_ = 0;
    GLuint vboKind_ = 0;
    GLuint vboShoreDistance_ = 0;
    GLuint fbo_ = 0;
    GLuint depthRbo_ = 0;
    GLuint radiusCubemap_ = 0;
    GLuint kindCubemap_ = 0;
    GLuint shoreDistanceCubemap_ = 0;

    GLint uViewProjection_ = -1;

    int resolution_ = 512;
    GLsizei vertexCount_ = 0;
    bool dirty_ = false;
    bool initializationFailed_ = false;
    BuildStats stats_{};
};
