#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QString>
#include <QVector3D>
#include <memory>
#include <unordered_map>
#include <vector>

class FactoryModelHandler : protected QOpenGLFunctions_3_3_Core {
public:
    FactoryModelHandler() = default;
    ~FactoryModelHandler();

    bool loadFromFile(const QString& path);
    void uploadToGPU();
    void draw(GLuint shader,
        const QMatrix4x4& mvp,
        const QMatrix4x4& modelMatrix,
        const QMatrix4x4& viewMatrix);
    void clear();
    void clearGPUResources();

    bool isReady() const { return glReady_ && !meshes_.empty(); }
    bool isEmpty() const { return meshes_.empty(); }
    QString loadedPath() const { return path_; }
    const QMatrix4x4& localPlacement() const { return localPlacement_; }

private:
    struct SubMesh {
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> texcoords;
        std::vector<float> colors;
        std::vector<uint32_t> indices;
        QString objectName;
        QString materialName;
        QString texturePath;
        QVector3D kdColor{ 1.0f, 1.0f, 1.0f };
        bool hasTexcoords = false;
        bool hasVertexColors = false;
        GLuint textureId = 0;
        GLuint vao = 0;
        GLuint vboPos = 0;
        GLuint vboNorm = 0;
        GLuint vboUV = 0;
        GLuint vboColor = 0;
        GLuint ibo = 0;
        GLsizei indexCount = 0;
    };

    void loadMaterials(const QString& objPath);
    GLuint loadTexture(const QString& path);
    void uploadSubMeshToGPU(SubMesh& sub);
    void clearSubMesh(SubMesh& sub);
    void resetDerivedPlacementData();
    void finalizePlacement();

    QString path_;
    QString materialLibraryPath_;
    std::vector<SubMesh> meshes_;
    std::unordered_map<QString, GLuint> textureCache_;
    QMatrix4x4 localPlacement_;
    bool glReady_ = false;
};
