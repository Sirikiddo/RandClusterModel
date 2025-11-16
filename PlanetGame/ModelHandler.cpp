#include "ModelHandler.h"
#include <QFileInfo>
#include <iostream>

ModelHandler::~ModelHandler() {
    clearGPUResources();
}

bool ModelHandler::loadFromFile(const std::string& path) {
    std::cout << "Loading model: " << path << std::endl;
    std::cout.flush();

    QFileInfo fi(QString::fromStdString(path));
    std::string ext = fi.suffix().toLower().toStdString();

    if (ext == "obj") {
        bool result = simple3d::load_obj_file(path, mesh_);
        if (result) {
            hasUVs_ = !mesh_.texcoords.empty(); // ← УСТАНАВЛИВАЕМ ФЛАГ
        }
        return result;
    }

    if (ext == "stl") {
        return simple3d::load_stl_file(path, mesh_);
    }
    else {
        std::cerr << "Unsupported format: " << ext << "\n";
        return false;
    }
}

void ModelHandler::uploadToGPU() {
    if (mesh_.positions.empty()) {
        std::cout << "No mesh data to upload" << std::endl;
        return;
    }

    std::cout << "Tree model has normals: " << (!mesh_.normals.empty() ? "YES" : "NO") << std::endl;
    std::cout << "Tree model has UVs: " << (hasUVs_ ? "YES" : "NO") << std::endl;
    std::cout << "Tree model vertices: " << mesh_.positions.size() / 3 << std::endl;
    std::cout << "Tree model UV count: " << mesh_.texcoords.size() / 2 << std::endl;

    if (!QOpenGLContext::currentContext()) {
        std::cerr << "No OpenGL context current!" << std::endl;
        return;
    }

    if (!glInitialized_) {
        initializeOpenGLFunctions();
        glInitialized_ = true;
        std::cout << "OpenGL functions initialized" << std::endl;
    }

    if (vao_ || vboPos_ || vboNorm_ || vboUV_ || ibo_) {
        clearGPUResources();
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vboPos_);
    glGenBuffers(1, &vboNorm_);
    if (hasUVs_) {
        glGenBuffers(1, &vboUV_);
    }
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

    // normals (optional)
    if (!mesh_.normals.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, vboNorm_);
        glBufferData(GL_ARRAY_BUFFER,
            mesh_.normals.size() * sizeof(float),
            mesh_.normals.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(1);
    }

    // UV coordinates (если есть)
    if (hasUVs_) {
        glBindBuffer(GL_ARRAY_BUFFER, vboUV_);
        glBufferData(GL_ARRAY_BUFFER,
            mesh_.texcoords.size() * sizeof(float),
            mesh_.texcoords.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(2);
        std::cout << "UV buffer enabled with " << mesh_.texcoords.size() / 2 << " coordinates" << std::endl;
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

void ModelHandler::draw(GLuint shader, const QMatrix4x4& mvp, const QMatrix4x4& modelMatrix, const QMatrix4x4& viewMatrix) {
    if (!vao_ || indexCount_ == 0 || !glInitialized_) return;

    glUseProgram(shader);

    // Передаём все необходимые uniform'ы
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
        QVector3D eye = (viewMatrix.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();
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
    if (QOpenGLContext::currentContext() && glInitialized_ && QOpenGLContext::currentContext()->isValid()) {
        if (vao_) {
            glDeleteVertexArrays(1, &vao_);
            vao_ = 0;
        }
        if (vboPos_) {
            glDeleteBuffers(1, &vboPos_);
            vboPos_ = 0;
        }
        if (vboNorm_) {
            glDeleteBuffers(1, &vboNorm_);
            vboNorm_ = 0;
        }
        if (vboUV_) {
            glDeleteBuffers(1, &vboUV_);
            vboUV_ = 0;
        }
        if (ibo_) {
            glDeleteBuffers(1, &ibo_);
            ibo_ = 0;
        }
    }
    else {
        vao_ = vboPos_ = vboNorm_ = vboUV_ = ibo_ = 0;
    }
    glInitialized_ = false;
}