#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>
#include <vector>
#include <unordered_map>
#include <memory>

class CarModelHandler : protected QOpenGLFunctions_3_3_Core {
public:
    CarModelHandler() = default;
    ~CarModelHandler();

    bool loadFromFile(const QString& path);
    void uploadToGPU();
    void draw(GLuint shader,
        const QMatrix4x4& mvp,
        const QMatrix4x4& modelMatrix,
        const QMatrix4x4& viewMatrix,
        float wheelSpinDegrees);
    void clear();
    void clearGPUResources();

    bool isReady() const { return glReady_ && !meshes_.empty(); }
    bool isEmpty() const { return meshes_.empty(); }
    QString loadedPath() const { return path_; }
    float wheelRadius() const { return averageWheelRadius_; }

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
        bool isWheel = false;
        QVector3D localCenter{ 0.0f, 0.0f, 0.0f };
        float localWheelRadius = 0.0f;
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

    QString path_;
    QString materialLibraryPath_;
    std::vector<SubMesh> meshes_;
    std::unordered_map<QString, GLuint> textureCache_;
    float averageWheelRadius_ = 0.0f;
    bool glReady_ = false;
};
