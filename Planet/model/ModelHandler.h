#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>  // Добавить этот include
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QString>

#include "simple3d_parser.hpp"

struct ModelPart {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<uint32_t> indices;
    std::string name;

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint vboNorm = 0;
    GLuint ibo = 0;
    GLsizei indexCount = 0;
    bool initialized = false;

    void clear() {
        positions.clear();
        normals.clear();
        texcoords.clear();
        indices.clear();
        indexCount = 0;
        initialized = false;
        if (vao) { /* очистка будет в clearGPUResources */ }
    }
};

class ModelHandler : protected QOpenGLFunctions_3_3_Core {
public:
    ModelHandler() = default;
    ~ModelHandler();

    bool loadFromFile(const QString& path);
    static std::shared_ptr<ModelHandler> loadShared(const QString& path);
    static void clearCache();

    void uploadToGPU();

    void drawPart(const QString& partName, GLuint shader,
        const QMatrix4x4& mvp,
        const QMatrix4x4& modelMatrix,
        const QMatrix4x4& viewMatrix);

    void draw(GLuint shader,
        const QMatrix4x4& mvp,
        const QMatrix4x4& modelMatrix,
        const QMatrix4x4& viewMatrix);

    bool hasPart(const QString& partName) const;

    void clear();
    void clearGPUResources();

    bool hasUVs() const { return hasUVs_; }
    bool hasNormals() const { return !mesh_.normals.empty(); }
    bool isInitialized() const { return glInitialized_; }
    bool isEmpty() const { return mesh_.positions.empty(); }
    QString loadedPath() const { return path_; }

private:
    static QString canonicalPath(const QString& path);
    void parsePartsFromMesh();

    static std::map<QString, std::weak_ptr<ModelHandler>> cache_;
    static std::mutex cacheMutex_;

    QString path_;
    simple3d::Mesh mesh_;
    std::map<std::string, ModelPart> parts_;

    GLsizei indexCount_ = 0;
    bool glInitialized_ = false;
    bool hasUVs_ = false;

    GLuint vao_ = 0;
    GLuint vboPos_ = 0;
    GLuint vboNorm_ = 0;
    GLuint vboUV_ = 0;
    GLuint ibo_ = 0;
};