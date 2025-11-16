#pragma once
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QString>
#include <string>
#include "simple3d_parser.hpp"

class ModelHandler : protected QOpenGLFunctions_3_3_Core {
public:
    ModelHandler() = default;
    ~ModelHandler();

    bool loadFromFile(const std::string& path);
    void uploadToGPU();
    void draw(GLuint shader, const QMatrix4x4& mvp, const QMatrix4x4& modelMatrix, const QMatrix4x4& viewMatrix);
    void clear();
    void clearGPUResources();

    // Геттеры для проверки состояния
    bool hasUVs() const { return hasUVs_; }
    bool hasNormals() const { return !mesh_.normals.empty(); }
    bool isInitialized() const { return glInitialized_ && vao_ != 0; }
    bool isEmpty() const { return mesh_.positions.empty(); }

private:
    simple3d::Mesh mesh_;
    GLsizei indexCount_ = 0;
    bool glInitialized_ = false;
    bool hasUVs_ = false; // ← ДОБАВИТЬ ЭТУ СТРОЧКУ!

    // Буферы
    GLuint vao_ = 0;
    GLuint vboPos_ = 0;
    GLuint vboNorm_ = 0;
    GLuint vboUV_ = 0;
    GLuint ibo_ = 0;
};