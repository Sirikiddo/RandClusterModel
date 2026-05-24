#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <vector>

#include "contributor/ContributorParticles.h"

class ParticleRenderer : protected QOpenGLFunctions_3_3_Core {
public:
    ParticleRenderer();
    ~ParticleRenderer();

    void initialize();
    void update(float deltaTime, const ContributorWindField& wind, const QVector3D& treeCenter);
    void updateParticles(const std::vector<ContributorParticle>& particles);
    void render(const QMatrix4x4& mvp, const QMatrix4x4& view, const QVector3D& cameraPos);
    bool isInitialized() const { return initialized_; }

private:
    void createShaders();
    void cleanup();

    GLuint program_ = 0;
    GLuint uMVP_ = -1;
    GLuint uView_ = -1;
    GLuint uViewPos_ = -1;
    GLuint uLightDir_ = -1;
    GLuint uTime_ = -1;

    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_;

    int particleCount_ = 0;
    bool initialized_ = false;
    float time_ = 0.0f;
};