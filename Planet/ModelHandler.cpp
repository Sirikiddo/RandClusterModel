#include "ModelHandler.h"
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QOpenGLContext>
#include <QMatrix4x4>

// ====== ПЕРЕХОД НА ПАРСЕР ЧЕРЕЗ POС ПОТОК ======
#include <sstream>

ModelHandler::~ModelHandler() {
    clearGPUResources();
}

bool ModelHandler::loadFromFile(const QString& path) {
    qDebug() << "Loading model:" << path;

    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();

    // ---------------------------
    // 1. ЧИТАЕМ ФАЙЛ ЧЕРЕЗ Qt
    // ---------------------------
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << path;
        return false;
    }

    QByteArray raw = file.readAll();
    file.close();

    // ---------------------------
    // 2. СОЗДАЁМ ПОТОК ДЛЯ ПАРСЕРА
    // ---------------------------
    std::istringstream stream(std::string(raw.constData(), raw.size()));

    bool result = false;

    // ---------------------------
    // 3. ВЫЗОВ ПАРСЕРА
    // ---------------------------
    if (ext == "obj") {
        result = simple3d::load_obj(stream, mesh_);
    }
    else if (ext == "stl") {
        result = simple3d::load_stl_binary(stream, mesh_);
    }
    else {
        qDebug() << "Unsupported format:" << ext;
        return false;
    }

    if (result) {
        hasUVs_ = !mesh_.texcoords.empty();
    }

    return result;
}

void ModelHandler::uploadToGPU() {
    if (mesh_.positions.empty()) {
        qDebug() << "No mesh data to upload";
        return;
    }

    qDebug() << "Tree model has normals:" << (!mesh_.normals.empty() ? "YES" : "NO");
    qDebug() << "Tree model has UVs:" << (hasUVs_ ? "YES" : "NO");
    qDebug() << "Tree model vertices:" << mesh_.positions.size() / 3;
    qDebug() << "Tree model UV count:" << mesh_.texcoords.size() / 2;

    if (!QOpenGLContext::currentContext()) {
        qDebug() << "No OpenGL context current!";
        return;
    }

    if (!glInitialized_) {
        initializeOpenGLFunctions();
        glInitialized_ = true;
        qDebug() << "OpenGL functions initialized";
    }

    if (vao_ || vboPos_ || vboNorm_ || vboUV_ || ibo_) {
        clearGPUResources();
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vboPos_);
    glGenBuffers(1, &vboNorm_);
    if (hasUVs_) glGenBuffers(1, &vboUV_);
    glGenBuffers(1, &ibo_);

    glBindVertexArray(vao_);

    // positions
    glBindBuffer(GL_ARRAY_BUFFER, vboPos_);
    glBufferData(GL_ARRAY_BUFFER,
        mesh_.positions.size() * sizeof(float),
        mesh_.positions.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    // normals
    if (!mesh_.normals.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, vboNorm_);
        glBufferData(GL_ARRAY_BUFFER,
            mesh_.normals.size() * sizeof(float),
            mesh_.normals.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(1);
    }

    // uvs
    if (hasUVs_) {
        glBindBuffer(GL_ARRAY_BUFFER, vboUV_);
        glBufferData(GL_ARRAY_BUFFER,
            mesh_.texcoords.size() * sizeof(float),
            mesh_.texcoords.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(2);
        qDebug() << "UV buffer enabled with" << mesh_.texcoords.size() / 2 << "coordinates";
    }

    // indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        mesh_.indices.size() * sizeof(uint32_t),
        mesh_.indices.data(),
        GL_STATIC_DRAW);

    indexCount_ = static_cast<GLsizei>(mesh_.indices.size());
    glBindVertexArray(0);
}

void ModelHandler::draw(GLuint shader,
    const QMatrix4x4& mvp,
    const QMatrix4x4& modelMatrix,
    const QMatrix4x4& viewMatrix)
{
    if (!vao_ || indexCount_ == 0 || !glInitialized_) return;

    glUseProgram(shader);

    GLint uMVP = glGetUniformLocation(shader, "uMVP");
    GLint uModel = glGetUniformLocation(shader, "uModel");
    GLint uLightDir = glGetUniformLocation(shader, "uLightDir");
    GLint uViewPos = glGetUniformLocation(shader, "uViewPos");
    GLint uColor = glGetUniformLocation(shader, "uColor");
    GLint uUseTexture = glGetUniformLocation(shader, "uUseTexture");

    if (uMVP >= 0) glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.constData());
    if (uModel >= 0) glUniformMatrix4fv(uModel, 1, GL_FALSE, modelMatrix.constData());
    if (uLightDir >= 0) glUniform3f(uLightDir, 0.5f, 1.0f, 0.3f);

    if (uViewPos >= 0) {
        QVector3D eye = (viewMatrix.inverted() *
            QVector4D(0, 0, 0, 1)).toVector3D();
        glUniform3f(uViewPos, eye.x(), eye.y(), eye.z());
    }

    if (uColor >= 0) glUniform3f(uColor, 0.15f, 0.5f, 0.1f);
    if (uUseTexture >= 0) glUniform1i(uUseTexture, hasUVs_ ? 1 : 0);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void ModelHandler::clear() {
    mesh_.clear();
    indexCount_ = 0;
    hasUVs_ = false;
    clearGPUResources();
}

void ModelHandler::clearGPUResources() {
    if (QOpenGLContext::currentContext() &&
        glInitialized_ &&
        QOpenGLContext::currentContext()->isValid())
    {
        if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
        if (vboPos_) { glDeleteBuffers(1, &vboPos_);   vboPos_ = 0; }
        if (vboNorm_) { glDeleteBuffers(1, &vboNorm_);  vboNorm_ = 0; }
        if (vboUV_) { glDeleteBuffers(1, &vboUV_);    vboUV_ = 0; }
        if (ibo_) { glDeleteBuffers(1, &ibo_);      ibo_ = 0; }
    }
    else {
        vao_ = vboPos_ = vboNorm_ = vboUV_ = ibo_ = 0;
    }
    glInitialized_ = false;
}
