#include "model/ModelHandler.h"
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QOpenGLContext>
#include <QMatrix4x4>

// ====== ПЕРЕХОД НА ПАРСЕР ЧЕРЕЗ POС ПОТОК ======
#include <sstream>

std::map<QString, std::weak_ptr<ModelHandler>>& ModelHandler::cache() {
    static auto* instance = new std::map<QString, std::weak_ptr<ModelHandler>>();
    return *instance;
}

std::mutex& ModelHandler::cacheMutex() {
    static auto* instance = new std::mutex();
    return *instance;
}

ModelHandler::~ModelHandler() {
    clearGPUResources();
}

std::shared_ptr<ModelHandler> ModelHandler::loadShared(const QString& path) {
    const QString normalized = ModelHandler::canonicalPath(path);

    std::lock_guard<std::mutex> lock(cacheMutex());
    auto& cacheRef = cache();
    auto it = cacheRef.find(normalized);
    if (it != cacheRef.end()) {
        if (auto cached = it->second.lock()) {
            qDebug() << "Reusing cached model for" << normalized;
            return cached;
        }
    }

    auto handler = std::shared_ptr<ModelHandler>(new ModelHandler());
    if (!handler->loadFromFile(normalized)) {
        return nullptr;
    }

    cacheRef[normalized] = handler;
    return handler;
}

void ModelHandler::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex());
    cache().clear();
}

void ModelHandler::parsePartsFromMesh() {
    parts_.clear();

    if (mesh_.positions.empty()) return;

    // Ствол - первые 8 вершин
    ModelPart trunk;
    trunk.name = "trunk";

    size_t trunkVertexCount = 8;
    size_t trunkVertexBytes = trunkVertexCount * 3;

    trunk.positions.assign(
        mesh_.positions.begin(),
        mesh_.positions.begin() + trunkVertexBytes
    );

    // Копируем нормали ствола
    if (!mesh_.normals.empty()) {
        trunk.normals.assign(
            mesh_.normals.begin(),
            mesh_.normals.begin() + trunkVertexBytes
        );
    }

    for (size_t i = 0; i < mesh_.indices.size(); i += 3) {
        uint32_t i0 = mesh_.indices[i];
        uint32_t i1 = mesh_.indices[i + 1];
        uint32_t i2 = mesh_.indices[i + 2];

        if (i0 < trunkVertexCount && i1 < trunkVertexCount && i2 < trunkVertexCount) {
            trunk.indices.push_back(i0);
            trunk.indices.push_back(i1);
            trunk.indices.push_back(i2);
        }
    }

    trunk.indexCount = trunk.indices.size();
    parts_["trunk"] = trunk;

    // Крона - остальные вершины
    ModelPart foliage;
    foliage.name = "foliage";

    size_t foliageStartVertex = 8;
    size_t foliageVertexCount = mesh_.vertexCount() - foliageStartVertex;

    if (foliageVertexCount > 0) {
        foliage.positions.assign(
            mesh_.positions.begin() + foliageStartVertex * 3,
            mesh_.positions.end()
        );

        // Копируем нормали кроны
        if (!mesh_.normals.empty()) {
            foliage.normals.assign(
                mesh_.normals.begin() + foliageStartVertex * 3,
                mesh_.normals.end()
            );
        }

        for (size_t i = 0; i < mesh_.indices.size(); i += 3) {
            uint32_t i0 = mesh_.indices[i];
            uint32_t i1 = mesh_.indices[i + 1];
            uint32_t i2 = mesh_.indices[i + 2];

            if (i0 >= foliageStartVertex && i1 >= foliageStartVertex && i2 >= foliageStartVertex) {
                foliage.indices.push_back(i0 - foliageStartVertex);
                foliage.indices.push_back(i1 - foliageStartVertex);
                foliage.indices.push_back(i2 - foliageStartVertex);
            }
        }

        foliage.indexCount = foliage.indices.size();
        parts_["foliage"] = foliage;
    }

    qDebug() << "Model split: trunk vertices" << trunk.positions.size() / 3
        << "normals" << (trunk.normals.empty() ? 0 : trunk.normals.size() / 3)
        << "indices" << trunk.indices.size()
        << "foliage vertices" << foliage.positions.size() / 3
        << "normals" << (foliage.normals.empty() ? 0 : foliage.normals.size() / 3)
        << "indices" << foliage.indices.size();
}

QString ModelHandler::canonicalPath(const QString& path) {
    QFileInfo fi(path);
    if (fi.exists()) {
        return fi.absoluteFilePath();
    }
    return path;
}

bool ModelHandler::loadFromFile(const QString& path) {
    const QString normalized = canonicalPath(path);

    if (path_ == normalized && !mesh_.positions.empty()) {
        qDebug() << "Model already loaded, reusing in-memory mesh for" << normalized;
        return true;
    }

    if (!path_.isEmpty() && path_ != normalized) {
        clear();
    }

    qDebug() << "Loading model:" << normalized;

    QFileInfo fi(normalized);
    QString ext = fi.suffix().toLower();

    // ---------------------------
    // 1. ЧИТАЕМ ФАЙЛ ЧЕРЕЗ Qt
    // ---------------------------
    QFile file(normalized);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << normalized;
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
        path_ = normalized;
        hasUVs_ = !mesh_.texcoords.empty();
    }
    else {
        mesh_.clear();
        path_.clear();
        hasUVs_ = false;
    }

    return result;
}

void ModelHandler::uploadToGPU() {
    if (mesh_.positions.empty()) {
        return;
    }

    parsePartsFromMesh();

    if (!QOpenGLContext::currentContext()) {
        return;
    }

    if (!glInitialized_) {
        initializeOpenGLFunctions();
        glInitialized_ = true;
    }

    // Загружаем каждую часть
    for (auto& [name, part] : parts_) {
        if (part.positions.empty() || part.indices.empty()) continue;

        glGenVertexArrays(1, &part.vao);
        glGenBuffers(1, &part.vbo);
        glGenBuffers(1, &part.ibo);

        glBindVertexArray(part.vao);

        // Позиции (location = 0)
        glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
        glBufferData(GL_ARRAY_BUFFER,
            part.positions.size() * sizeof(float),
            part.positions.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);

        // Нормали (location = 1) - если есть
        if (!part.normals.empty()) {
            // Создаем отдельный VBO для нормалей или используем тот же с оффсетом
            // Для простоты создадим отдельный VBO
            GLuint vboNorm;
            glGenBuffers(1, &vboNorm);
            glBindBuffer(GL_ARRAY_BUFFER, vboNorm);
            glBufferData(GL_ARRAY_BUFFER,
                part.normals.size() * sizeof(float),
                part.normals.data(),
                GL_STATIC_DRAW);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(1);
            // Сохраняем в part для очистки позже
            const_cast<ModelPart&>(part).vboNorm = vboNorm;
        }
        else {
            // Если нет нормалей, используем заглушку (вверх)
            // Это не идеально, но лучше чем ничего
            qDebug() << "Warning: Part" << name << "has no normals!";
        }

        // Индексы
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            part.indices.size() * sizeof(uint32_t),
            part.indices.data(),
            GL_STATIC_DRAW);

        part.indexCount = part.indices.size();
        part.initialized = true;

        glBindVertexArray(0);

        qDebug() << "Uploaded part:" << name
            << "vertices:" << part.positions.size() / 3
            << "normals:" << (part.normals.empty() ? 0 : part.normals.size() / 3)
            << "indices:" << part.indices.size();
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

void ModelHandler::drawPart(const QString& partName, GLuint shader,
    const QMatrix4x4& mvp,
    const QMatrix4x4& modelMatrix,
    const QMatrix4x4& viewMatrix) {
    // Конвертируем QString в std::string для поиска в map
    auto it = parts_.find(partName);
    if (it == parts_.end() || !it->second.initialized || it->second.indexCount == 0) {
        return;
    }

    const ModelPart& part = it->second;

    glUseProgram(shader);

    GLint uMVP = glGetUniformLocation(shader, "uMVP");
    GLint uModel = glGetUniformLocation(shader, "uModel");

    if (uMVP >= 0) glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.constData());
    if (uModel >= 0) glUniformMatrix4fv(uModel, 1, GL_FALSE, modelMatrix.constData());

    glBindVertexArray(part.vao);
    glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

bool ModelHandler::hasPart(const QString& partName) const {
    return parts_.find(partName) != parts_.end();
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
    path_.clear();
    indexCount_ = 0;
    hasUVs_ = false;
    clearGPUResources();
}

void ModelHandler::clearGPUResources() {
    if (QOpenGLContext::currentContext() && glInitialized_) {
        for (auto& [name, part] : parts_) {
            if (part.vao) glDeleteVertexArrays(1, &part.vao);
            if (part.vbo) glDeleteBuffers(1, &part.vbo);
            if (part.vboNorm) glDeleteBuffers(1, &part.vboNorm);
            if (part.ibo) glDeleteBuffers(1, &part.ibo);
            part.initialized = false;
        }
        parts_.clear();

        // Очистка старых ресурсов для обратной совместимости
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vboPos_) glDeleteBuffers(1, &vboPos_);
        if (vboNorm_) glDeleteBuffers(1, &vboNorm_);
        if (vboUV_) glDeleteBuffers(1, &vboUV_);
        if (ibo_) glDeleteBuffers(1, &ibo_);

        vao_ = vboPos_ = vboNorm_ = vboUV_ = ibo_ = 0;
    }
    glInitialized_ = false;
}
