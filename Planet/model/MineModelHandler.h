#pragma once

#include <QMatrix4x4>
#include <QElapsedTimer>
#include <QOpenGLFunctions_3_3_Core>
#include <QString>
#include <QVector3D>
#include <memory>
#include <unordered_map>
#include <vector>

class MineModelHandler : protected QOpenGLFunctions_3_3_Core {
public:
    MineModelHandler() = default;
    ~MineModelHandler();

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
        QString diffuseTexturePath;
        QString metallicRoughnessPath;
        QString normalTexturePath;
        QVector3D kdColor{ 1.0f, 1.0f, 1.0f };
        bool hasTexcoords = false;
        bool hasVertexColors = false;
        bool isCart = false;
        QVector3D localCenter{ 0.0f, 0.0f, 0.0f };
        QVector3D cartTravelAxis{ 0.0f, 0.0f, 0.0f };
        float cartTravelDistance = 0.0f;
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
    void resetDerivedPlacementData();
    void finalizePlacement();
    void finalizeCartAnimation();

    QString path_;
    QString materialLibraryPath_;
    std::vector<SubMesh> meshes_;
    std::unordered_map<QString, GLuint> textureCache_;
    QMatrix4x4 localPlacement_;
    mutable QElapsedTimer cartAnimationTimer_;
    bool glReady_ = false;
};
