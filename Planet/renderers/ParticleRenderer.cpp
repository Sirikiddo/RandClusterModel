#include "renderers/ParticleRenderer.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

static const char* vertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in float aSize;
layout(location = 3) in float aRotation;
layout(location = 4) in vec3 aNormal;

uniform mat4 uMVP;
uniform vec3 uViewPos;
uniform vec3 uLightDir;

out vec3 vColor;
out float vAlpha;
out float vRotation;
out float vDiffuse;

void main() {
    vColor = aColor;
    vRotation = aRotation;
    
    vec3 N = normalize(aNormal);
    vec3 L = normalize(uLightDir);
    float NdotL = dot(N, L);
    vDiffuse = max(NdotL, 0.0) * 0.6 + 0.4;
    
    float dist = length(uViewPos - aPos);
    float pointSize = aSize * (300.0 / dist);
    gl_PointSize = clamp(pointSize, 8.0, 45.0);
    
    gl_Position = uMVP * vec4(aPos, 1.0);
    
    float distFactor = clamp(1.0 - dist * 0.3, 0.4, 0.9);
    vAlpha = distFactor;
}
)";

static const char* fragmentShader = R"(
#version 330 core
in vec3 vColor;
in float vAlpha;
in float vRotation;
in float vDiffuse;

out vec4 FragColor;

void main() {
    vec2 uv = gl_PointCoord;
    vec2 p = uv - 0.5;
    
    float cosR = cos(vRotation);
    float sinR = sin(vRotation);
    vec2 rotatedP;
    rotatedP.x = p.x * cosR - p.y * sinR;
    rotatedP.y = p.x * sinR + p.y * cosR;
    
    rotatedP.y = rotatedP.y * 3.0;
    
    float ellipse = (rotatedP.x * rotatedP.x) / (0.50 * 0.50) + (rotatedP.y * rotatedP.y) / (0.65 * 0.65);
    
    float tip = 1.0 - abs(rotatedP.y) * 0.5;
    tip = max(tip, 0.4);
    
    float finalShape = ellipse / tip;
    
    float alpha = 1.0 - smoothstep(0.9, 1.1, finalShape);
    
    alpha = alpha * vAlpha * 0.9;
    
    if (alpha < 0.05) discard;
    
    vec3 finalColor = vColor * vDiffuse;
    FragColor = vec4(finalColor, alpha);
}
)";

ParticleRenderer::ParticleRenderer() {
}

ParticleRenderer::~ParticleRenderer() {
    cleanup();
}

void ParticleRenderer::cleanup() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vbo_.isCreated()) {
        vbo_.destroy();
    }
    if (vao_.isCreated()) {
        vao_.destroy();
    }
    initialized_ = false;
}

void ParticleRenderer::initialize() {
    if (initialized_) return;

    initializeOpenGLFunctions();

    createShaders();

    vao_.create();
    vbo_.create();

    vao_.bind();
    vbo_.bind();

    // Позиции (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ContributorParticle),
        reinterpret_cast<void*>(offsetof(ContributorParticle, position)));

    // Цвета (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ContributorParticle),
        reinterpret_cast<void*>(offsetof(ContributorParticle, color)));

    // Размер (location = 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ContributorParticle),
        reinterpret_cast<void*>(offsetof(ContributorParticle, size)));

    // Вращение (location = 3)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ContributorParticle),
        reinterpret_cast<void*>(offsetof(ContributorParticle, rotation)));

    // Нормали (location = 4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(ContributorParticle),
        reinterpret_cast<void*>(offsetof(ContributorParticle, normal)));

    vao_.release();
    vbo_.release();

    initialized_ = true;
    qDebug() << "ParticleRenderer initialized with normals";
}
void ParticleRenderer::createShaders() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShader, nullptr);
    glCompileShader(vs);

    GLint success;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vs, 512, nullptr, infoLog);
        qDebug() << "Vertex shader compilation failed:" << infoLog;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShader, nullptr);
    glCompileShader(fs);

    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(fs, 512, nullptr, infoLog);
        qDebug() << "Fragment shader compilation failed:" << infoLog;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program_, 512, nullptr, infoLog);
        qDebug() << "Shader program linking failed:" << infoLog;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    uMVP_ = glGetUniformLocation(program_, "uMVP");
    uView_ = glGetUniformLocation(program_, "uView");
    uViewPos_ = glGetUniformLocation(program_, "uViewPos");
    uLightDir_ = glGetUniformLocation(program_, "uLightDir");
    uTime_ = glGetUniformLocation(program_, "uTime");

    qDebug() << "ParticleRenderer shaders created, uMVP:" << uMVP_ << "uViewPos:" << uViewPos_;
}

void ParticleRenderer::updateParticles(const std::vector<ContributorParticle>& particles) {
    if (!initialized_ || particles.empty()) return;

    particleCount_ = static_cast<int>(particles.size());

    vbo_.bind();
    vbo_.allocate(particles.data(), static_cast<int>(particles.size() * sizeof(ContributorParticle)));
    vbo_.release();

    qDebug() << "ParticleRenderer updated with" << particleCount_ << "particles";
}

void ParticleRenderer::render(const QMatrix4x4& mvp, const QMatrix4x4& view, const QVector3D& cameraPos) {
    if (!initialized_ || particleCount_ == 0) return;

    time_ += 0.016f;

    glUseProgram(program_);
    glUniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp.constData());
    glUniform3f(uViewPos_, cameraPos.x(), cameraPos.y(), cameraPos.z());
    glUniform1f(uTime_, time_);

    QVector3D lightDir = QVector3D(0.5f, 1.0f, 0.3f).normalized();
    glUniform3f(uLightDir_, lightDir.x(), lightDir.y(), lightDir.z());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_CULL_FACE);

    vao_.bind();
    glDrawArrays(GL_POINTS, 0, particleCount_);
    vao_.release();

    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}
void ParticleRenderer::update(float deltaTime, const ContributorWindField& wind, const QVector3D& treeCenter) {
    if (!initialized_ || particleCount_ == 0) return;

    // Ограничиваем deltaTime
    deltaTime = std::min(deltaTime, 0.033f);

    // Получаем указатель на данные частиц
    vbo_.bind();
    ContributorParticle* particles = reinterpret_cast<ContributorParticle*>(vbo_.map(QOpenGLBuffer::ReadWrite));

    if (!particles) {
        vbo_.unmap();
        vbo_.release();
        return;
    }

    const float stiffness = 8.0f;   // Сила возврата к restPosition
    const float damping = 3.0f;     // Затухание

    for (int i = 0; i < particleCount_; ++i) {
        ContributorParticle& p = particles[i];

        // 1. Wind force (ветер)
        float heightFactor = (p.restPosition.y() - treeCenter.y()) / 5.0f;
        heightFactor = std::clamp(heightFactor, 0.3f, 1.2f);

        float gust = std::sin(time_ * wind.gustSpeed + p.phase) * wind.gustStrength;
        float turbulenceX = std::sin(time_ * 2.3f + p.restPosition.y() * 1.5f) * wind.turbulence;
        float turbulenceZ = std::cos(time_ * 1.7f + p.restPosition.x() * 1.2f) * wind.turbulence;

        QVector3D windForce = wind.direction * (wind.strength + gust) * heightFactor;
        windForce.setX(windForce.x() + turbulenceX);
        windForce.setZ(windForce.z() + turbulenceZ);
        windForce *= p.windWeight;

        // 2. Spring force (возврат к restPosition)
        QVector3D springForce = (p.restPosition - p.position) * stiffness;

        // 3. Damping force (затухание)
        QVector3D dampingForce = -p.velocity * damping;

        // Суммарная сила
        QVector3D acceleration = windForce + springForce + dampingForce;

        // Обновление скорости и позиции
        p.velocity += acceleration * deltaTime;
        p.position += p.velocity * deltaTime;

        // Небольшое затухание скорости
        p.velocity *= 0.99f;
    }

    vbo_.unmap();
    vbo_.release();
}
