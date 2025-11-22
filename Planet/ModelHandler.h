#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QString>

#include "simple3d_parser.hpp"    // парсер остаётся

class ModelHandler : protected QOpenGLFunctions_3_3_Core {
public:
    ModelHandler() = default;
    ~ModelHandler();

    // загрузка только через QString
    bool loadFromFile(const QString& path);

    // Получение готовой модели из кеша (CPU + GPU)
    static std::shared_ptr<ModelHandler> loadShared(const QString& path);
    static void clearCache();

    void uploadToGPU();
    void draw(GLuint shader,
        const QMatrix4x4& mvp,
        const QMatrix4x4& modelMatrix,
        const QMatrix4x4& viewMatrix);

    void clear();
    void clearGPUResources();

    // ---- getters ----
    bool hasUVs() const { return hasUVs_; }
    bool hasNormals() const { return !mesh_.normals.empty(); }
    bool isInitialized() const { return glInitialized_ && vao_ != 0; }
    bool isEmpty() const { return mesh_.positions.empty(); }
    QString loadedPath() const { return path_; }

private:
    static QString canonicalPath(const QString& path);

    static std::map<QString, std::weak_ptr<ModelHandler>> cache_;
    static std::mutex cacheMutex_;

    QString path_;
    simple3d::Mesh mesh_;      // без std::string внутри
    GLsizei indexCount_ = 0;

    bool glInitialized_ = false;
    bool hasUVs_ = false;

    // OpenGL buffers
    GLuint vao_ = 0;
    GLuint vboPos_ = 0;
    GLuint vboNorm_ = 0;
    GLuint vboUV_ = 0;
    GLuint ibo_ = 0;
};