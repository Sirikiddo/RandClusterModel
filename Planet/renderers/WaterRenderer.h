#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QtOpenGL>

#include "renderers/HexSphereRenderer.h"
#include "renderers/WaterShaderUniforms.h"

class WaterRenderer {
public:
    struct Resources {
        GLuint envCubemap = 0;
        GLuint sceneDepthTexture = 0;
        GLuint planetRadiusAtlas = 0;
        GLuint planetSurfaceKindAtlas = 0;
        GLuint planetShoreDistanceAtlas = 0;
        GLuint vao = 0;
    };

    WaterRenderer(QOpenGLFunctions_3_3_Core* gl, GLuint program);

    void render(const HexSphereRenderer::RenderContext& ctx, const Resources& resources) const;

private:
    GLint uniform(const char* name) const;

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint program_ = 0;

    // Всё, что общее с проходом рек (волны, оптика, окружение).
    WaterShaderLocations commonLocations_;

    // Только ray-marching морской оболочки.
    GLint uWavesEnabled_ = -1;
    GLint uWaterShellInnerRadius_ = -1;
    GLint uWaterShellOuterRadius_ = -1;
    GLint uBottomEpsilon_ = -1;
    GLint uBoundEpsilon_ = -1;
    GLint uHitEpsilon_ = -1;
    GLint uTerrainContactEpsilon_ = -1;
    GLint uMaximumPhaseGradient_ = -1;
    GLint uMaximumWaveAngularGradient_ = -1;
    GLint uMaximumAmplitudeGradient_ = -1;
    GLint uSceneDepthTexture_ = -1;
    GLint uPlanetRadiusAtlas_ = -1;
    GLint uPlanetSurfaceKindAtlas_ = -1;
    GLint uPlanetShoreDistanceAtlas_ = -1;
};
