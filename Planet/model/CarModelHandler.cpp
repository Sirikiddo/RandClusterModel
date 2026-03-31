#include "model/CarModelHandler.h"
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDebug>
#include <QOpenGLContext>
#include <sstream>
#include <charconv>
#include <QRegularExpression>

CarModelHandler::~CarModelHandler() {
    clearGPUResources();
}

static QString canonicalPath(const QString& path) {
    QFileInfo fi(path);
    if (fi.exists()) {
        return fi.absoluteFilePath();
    }
    return path;
}

bool CarModelHandler::loadFromFile(const QString& path) {
    const QString normalized = canonicalPath(path);

    if (path_ == normalized && !meshes_.empty()) {
        return true;
    }

    if (!path_.isEmpty() && path_ != normalized) {
        clear();
    }

    qDebug() << "Loading car model:" << normalized;

    QFileInfo fi(normalized);
    QString ext = fi.suffix().toLower();
    if (ext != "obj") {
        qDebug() << "CarModelHandler only supports OBJ format";
        return false;
    }

    QFile file(normalized);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << normalized;
        return false;
    }

    QByteArray raw = file.readAll();
    file.close();

    std::istringstream stream(std::string(raw.constData(), raw.size()));

    std::vector<float> tmpPos, tmpNorm, tmpUV;
    std::string line;

    struct FaceCorner {
        int v = 0, vt = -1, vn = -1;
    };

    std::unordered_map<QString, std::vector<FaceCorner>> materialFaces;
    QString currentMaterial;

    auto parseFaceCorner = [](const std::string_view& tok) -> FaceCorner {
        FaceCorner fc;
        int slash1 = -1, slash2 = -1;
        for (size_t i = 0; i < tok.size(); ++i) {
            if (tok[i] == '/') {
                if (slash1 < 0) slash1 = (int)i;
                else { slash2 = (int)i; break; }
            }
        }

        auto parse_int = [](std::string_view s, int& x) -> bool {
            const char* first = s.data();
            const char* last = first + s.size();
            auto res = std::from_chars(first, last, x);
            return res.ec == std::errc{};
            };

        if (slash1 < 0) {
            parse_int(tok, fc.v);
        }
        else if (slash2 < 0) {
            parse_int(tok.substr(0, slash1), fc.v);
            std::string_view b = tok.substr(slash1 + 1);
            if (b.size()) parse_int(b, fc.vt);
        }
        else {
            parse_int(tok.substr(0, slash1), fc.v);
            std::string_view vt = tok.substr(slash1 + 1, slash2 - slash1 - 1);
            if (vt.size()) parse_int(vt, fc.vt);
            std::string_view vn = tok.substr(slash2 + 1);
            if (vn.size()) parse_int(vn, fc.vn);
        }
        return fc;
        };

    while (std::getline(stream, line)) {
        std::string_view sv(line);
        // trim
        size_t start = 0;
        while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\r')) ++start;
        size_t end = sv.size();
        while (end > start && (sv[end - 1] == ' ' || sv[end - 1] == '\t' || sv[end - 1] == '\r' || sv[end - 1] == '\n')) --end;
        sv = sv.substr(start, end - start);

        if (sv.empty() || sv[0] == '#') continue;

        if (sv.size() >= 2 && sv[0] == 'v' && sv[1] == ' ') {
            // vertex
            std::vector<std::string_view> toks;
            size_t i = 2;
            while (i < sv.size()) {
                while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t')) ++i;
                if (i >= sv.size()) break;
                size_t j = i;
                while (j < sv.size() && sv[j] != ' ' && sv[j] != '\t') ++j;
                toks.push_back(sv.substr(i, j - i));
                i = j;
            }
            if (toks.size() >= 3) {
                float x, y, z;
                std::from_chars(toks[0].data(), toks[0].data() + toks[0].size(), x);
                std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), y);
                std::from_chars(toks[2].data(), toks[2].data() + toks[2].size(), z);
                tmpPos.insert(tmpPos.end(), { x, y, z });
            }
        }
        else if (sv.size() >= 3 && sv[0] == 'v' && sv[1] == 't' && sv[2] == ' ') {
            // texture coord
            std::vector<std::string_view> toks;
            size_t i = 3;
            while (i < sv.size()) {
                while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t')) ++i;
                if (i >= sv.size()) break;
                size_t j = i;
                while (j < sv.size() && sv[j] != ' ' && sv[j] != '\t') ++j;
                toks.push_back(sv.substr(i, j - i));
                i = j;
            }
            if (toks.size() >= 2) {
                float u, v;
                std::from_chars(toks[0].data(), toks[0].data() + toks[0].size(), u);
                std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), v);
                tmpUV.insert(tmpUV.end(), { u, v });
            }
        }
        else if (sv.size() >= 3 && sv[0] == 'v' && sv[1] == 'n' && sv[2] == ' ') {
            // normal
            std::vector<std::string_view> toks;
            size_t i = 3;
            while (i < sv.size()) {
                while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t')) ++i;
                if (i >= sv.size()) break;
                size_t j = i;
                while (j < sv.size() && sv[j] != ' ' && sv[j] != '\t') ++j;
                toks.push_back(sv.substr(i, j - i));
                i = j;
            }
            if (toks.size() >= 3) {
                float x, y, z;
                std::from_chars(toks[0].data(), toks[0].data() + toks[0].size(), x);
                std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), y);
                std::from_chars(toks[2].data(), toks[2].data() + toks[2].size(), z);
                tmpNorm.insert(tmpNorm.end(), { x, y, z });
            }
        }
        else if (sv.rfind("usemtl ", 0) == 0) {
            currentMaterial = QString::fromStdString(std::string(sv.substr(7)));
        }
        else if (sv.size() >= 2 && sv[0] == 'f' && sv[1] == ' ') {
            // face
            std::vector<std::string_view> toks;
            size_t i = 2;
            while (i < sv.size()) {
                while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t')) ++i;
                if (i >= sv.size()) break;
                size_t j = i;
                while (j < sv.size() && sv[j] != ' ' && sv[j] != '\t') ++j;
                toks.push_back(sv.substr(i, j - i));
                i = j;
            }
            if (toks.size() >= 3) {
                std::vector<FaceCorner> corners;
                for (auto tok : toks) {
                    corners.push_back(parseFaceCorner(tok));
                }
                // triangulate fan
                for (size_t k = 1; k + 1 < corners.size(); ++k) {
                    materialFaces[currentMaterial].push_back(corners[0]);
                    materialFaces[currentMaterial].push_back(corners[k]);
                    materialFaces[currentMaterial].push_back(corners[k + 1]);
                }
            }
        }
    }

    // Build meshes per material
    for (auto& [matName, faces] : materialFaces) {
        if (faces.empty()) continue;

        SubMesh sub;
        sub.materialName = matName;

        std::unordered_map<uint64_t, uint32_t> vertexMap;
        auto addVertex = [&](const FaceCorner& fc) -> uint32_t {
            int v = fc.v > 0 ? fc.v - 1 : (int)(tmpPos.size() / 3) + fc.v;
            int vt = fc.vt >= 0 ? (fc.vt > 0 ? fc.vt - 1 : (int)(tmpUV.size() / 2) + fc.vt) : -1;
            int vn = fc.vn >= 0 ? (fc.vn > 0 ? fc.vn - 1 : (int)(tmpNorm.size() / 3) + fc.vn) : -1;

            uint64_t key = (uint64_t(v) << 32) | (uint64_t(vt) << 16) | uint64_t(vn);
            auto it = vertexMap.find(key);
            if (it != vertexMap.end()) return it->second;

            uint32_t idx = (uint32_t)(sub.positions.size() / 3);
            sub.positions.push_back(tmpPos[v * 3]);
            sub.positions.push_back(tmpPos[v * 3 + 1]);
            sub.positions.push_back(tmpPos[v * 3 + 2]);

            if (vn >= 0 && (size_t)vn * 3 + 2 < tmpNorm.size()) {
                sub.normals.push_back(tmpNorm[vn * 3]);
                sub.normals.push_back(tmpNorm[vn * 3 + 1]);
                sub.normals.push_back(tmpNorm[vn * 3 + 2]);
            }

            if (vt >= 0 && (size_t)vt * 2 + 1 < tmpUV.size()) {
                sub.texcoords.push_back(tmpUV[vt * 2]);
                sub.texcoords.push_back(tmpUV[vt * 2 + 1]);
            }

            vertexMap[key] = idx;
            return idx;
            };

        for (size_t f = 0; f < faces.size(); f += 3) {
            uint32_t i0 = addVertex(faces[f]);
            uint32_t i1 = addVertex(faces[f + 1]);
            uint32_t i2 = addVertex(faces[f + 2]);
            sub.indices.push_back(i0);
            sub.indices.push_back(i1);
            sub.indices.push_back(i2);
        }

        if (!sub.positions.empty()) {
            meshes_.push_back(std::move(sub));
        }
    }

    if (meshes_.empty()) {
        qDebug() << "No mesh data loaded for car model";
        return false;
    }

    path_ = normalized;
    loadMaterials(normalized);
    return true;
}

// �������� ����� loadMaterials �� ����:
void CarModelHandler::loadMaterials(const QString& objPath) {
    QFileInfo objInfo(objPath);
    QString mtlPath = objInfo.path() + "/" + objInfo.baseName() + ".mtl";
    QFile mtlFile(mtlPath);
    if (!mtlFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open MTL file:" << mtlPath;
        return;
    }

    QString currentMat;
    std::map<QString, QString> textureMap;
    std::map<QString, QVector3D> kdMap;

    QVector3D currentKd(1.0f, 1.0f, 1.0f);

    QRegularExpression wsRe(QStringLiteral("\\s+"));
    while (!mtlFile.atEnd()) {
        const QString line = QString::fromUtf8(mtlFile.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        const QStringList parts = line.split(wsRe, Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        if (parts[0] == "newmtl") {
            if (parts.size() >= 2) {
                currentMat = parts[1];
            }
            currentKd = QVector3D(1.0f, 1.0f, 1.0f);
        }
        else if (parts[0] == "Kd" && parts.size() >= 4) {
            // Diffuse color (Kd r g b)
            const float r = parts[1].toFloat();
            const float g = parts[2].toFloat();
            const float b = parts[3].toFloat();
            currentKd = QVector3D(r, g, b);
            kdMap[currentMat] = currentKd;
        }
        else if (parts[0] == "map_Kd" && parts.size() >= 2) {
            QString texPath = parts[1];
            if (QFileInfo(texPath).isRelative()) {
                texPath = objInfo.path() + "/" + texPath;
            }
            textureMap[currentMat] = texPath;
        }
    }
    mtlFile.close();

    // Сохраняем пути к текстурам и цвета диффузии (Kd), но НЕ загружаем текстуры сейчас
    for (auto& sub : meshes_) {
        auto it = textureMap.find(sub.materialName);
        if (it != textureMap.end()) {
            sub.texturePath = it->second;  // Нужно добавить поле texturePath в SubMesh
        }

        auto itKd = kdMap.find(sub.materialName);
        if (itKd != kdMap.end()) {
            sub.kdColor = itKd->second;
        }
    }
}

GLuint CarModelHandler::loadTexture(const QString& path) {
    // Проверяем, есть ли активный контекст
    if (!QOpenGLContext::currentContext()) {
        qDebug() << "Cannot load texture" << path << "- no OpenGL context";
        return 0;
    }

    auto cached = textureCache_.find(path);
    if (cached != textureCache_.end()) {
        return cached->second;
    }

    QImage img;
    if (!img.load(path)) {
        qDebug() << "Failed to load texture:" << path;
        return 0;
    }

    img = img.convertToFormat(QImage::Format_RGBA8888);

    GLuint texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(),
        0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    textureCache_[path] = texId;
    return texId;
}

void CarModelHandler::uploadToGPU() {
    if (!QOpenGLContext::currentContext()) return;
    if (!glReady_) {
        initializeOpenGLFunctions();
        glReady_ = true;
    }

    for (auto& sub : meshes_) {
        uploadSubMeshToGPU(sub);
    }
}

void CarModelHandler::uploadSubMeshToGPU(SubMesh& sub) {
    if (sub.positions.empty()) return;

    // Проверяем, есть ли активный контекст
    if (!QOpenGLContext::currentContext()) {
        qDebug() << "No OpenGL context for uploading submesh!";
        return;
    }

    glGenVertexArrays(1, &sub.vao);
    glGenBuffers(1, &sub.vboPos);
    if (!sub.normals.empty()) glGenBuffers(1, &sub.vboNorm);
    if (!sub.texcoords.empty()) glGenBuffers(1, &sub.vboUV);
    glGenBuffers(1, &sub.ibo);

    glBindVertexArray(sub.vao);

    glBindBuffer(GL_ARRAY_BUFFER, sub.vboPos);
    glBufferData(GL_ARRAY_BUFFER,
        sub.positions.size() * sizeof(float),
        sub.positions.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    if (!sub.normals.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, sub.vboNorm);
        glBufferData(GL_ARRAY_BUFFER,
            sub.normals.size() * sizeof(float),
            sub.normals.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(1);
    }

    if (!sub.texcoords.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, sub.vboUV);
        glBufferData(GL_ARRAY_BUFFER,
            sub.texcoords.size() * sizeof(float),
            sub.texcoords.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(2);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sub.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        sub.indices.size() * sizeof(uint32_t),
        sub.indices.data(),
        GL_STATIC_DRAW);

    sub.indexCount = static_cast<GLsizei>(sub.indices.size());
    glBindVertexArray(0);

    // Загружаем текстуру ТОЛЬКО здесь, когда контекст активен
    if (!sub.texturePath.isEmpty() && sub.textureId == 0) {
        sub.textureId = loadTexture(sub.texturePath);
    }
}

void CarModelHandler::draw(GLuint shader,
    const QMatrix4x4& mvp,
    const QMatrix4x4& modelMatrix,
    const QMatrix4x4& viewMatrix) {
    if (!glReady_ || meshes_.empty()) return;

    glUseProgram(shader);

    GLint uMVP = glGetUniformLocation(shader, "uMVP");
    GLint uModel = glGetUniformLocation(shader, "uModel");
    GLint uViewPos = glGetUniformLocation(shader, "uViewPos");
    GLint uLightDir = glGetUniformLocation(shader, "uLightDir");
    GLint uUseTexture = glGetUniformLocation(shader, "uUseTexture");
    GLint uTexture = glGetUniformLocation(shader, "uTexture");
    GLint uColor = glGetUniformLocation(shader, "uColor");  // <-- получаем location цвета

    if (uMVP >= 0) glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.constData());
    if (uModel >= 0) glUniformMatrix4fv(uModel, 1, GL_FALSE, modelMatrix.constData());

    QVector3D eye = (viewMatrix.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();
    if (uViewPos >= 0) glUniform3f(uViewPos, eye.x(), eye.y(), eye.z());
    if (uLightDir >= 0) glUniform3f(uLightDir, 0.5f, 1.0f, 0.3f);

    // ������������� uColor per-submesh �� Kd (���� �������� ��� � ����� ��������� ���� MTL).

    for (auto& sub : meshes_) {
        if (sub.indexCount == 0) continue;

        // Lazy texture load:
        // � ��������� ��������� �������� ����� �� ������ ������������ � uploadToGPU().
        // �����, ����� ��������� �������� ����� �������, ��������� ��� �������������.
        if (sub.textureId == 0 && !sub.texturePath.isEmpty()) {
            // Note: loadTexture ���������� QOpenGLContext::currentContext().
            // ���� ��������� ��� � �������� 0, � shader ������ fallback.
            sub.textureId = loadTexture(sub.texturePath);
        }

        if (uColor >= 0) {
            glUniform3f(uColor, sub.kdColor.x(), sub.kdColor.y(), sub.kdColor.z());
        }

        if (uUseTexture >= 0) {
            // Используем текстуры, если они есть
            // �����: �������� ������ �������� ��� UV.
            // ���� vboUV �� ������ (��� texcoords), vUV � ��������� ������� ����� �����������������,
            // � ����� �������� ����� ��������� ��� "���������".
            // ������������� �� ������� texcoords � �������� ���������.
            // ��� �������, ��� ��������� vboUV (�� ������� �� ����, ����� �� ����������� �����).
            int useTex = (sub.textureId != 0 && !sub.texcoords.empty()) ? 1 : 0;
            glUniform1i(uUseTexture, useTex);

            if (sub.textureId != 0 && uTexture >= 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sub.textureId);
                glUniform1i(uTexture, 0);
            }
        }

        glBindVertexArray(sub.vao);
        glDrawElements(GL_TRIANGLES, sub.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
}

void CarModelHandler::clear() {
    clearGPUResources();
    meshes_.clear();
    path_.clear();
}

// Исправить clearGPUResources — добавить проверку контекста:
void CarModelHandler::clearGPUResources() {
    // Проверяем, есть ли активный контекст перед удалением
    bool hasContext = QOpenGLContext::currentContext() != nullptr;

    for (auto& sub : meshes_) {
        if (hasContext && sub.vao) {
            glDeleteVertexArrays(1, &sub.vao);
            glDeleteBuffers(1, &sub.vboPos);
            if (sub.vboNorm) glDeleteBuffers(1, &sub.vboNorm);
            if (sub.vboUV) glDeleteBuffers(1, &sub.vboUV);
            if (sub.ibo) glDeleteBuffers(1, &sub.ibo);
        }
        if (sub.textureId && hasContext) {
            glDeleteTextures(1, &sub.textureId);
        }
        sub = SubMesh{};
    }

    if (hasContext) {
        for (auto& [path, tex] : textureCache_) {
            if (tex) glDeleteTextures(1, &tex);
        }
    }
    textureCache_.clear();

    glReady_ = false;
}

void CarModelHandler::clearSubMesh(SubMesh& sub) {
    if (sub.vao && QOpenGLContext::currentContext()) {
        glDeleteVertexArrays(1, &sub.vao);
        glDeleteBuffers(1, &sub.vboPos);
        if (sub.vboNorm) glDeleteBuffers(1, &sub.vboNorm);
        if (sub.vboUV) glDeleteBuffers(1, &sub.vboUV);
        if (sub.ibo) glDeleteBuffers(1, &sub.ibo);
    }
    sub = SubMesh{};
}