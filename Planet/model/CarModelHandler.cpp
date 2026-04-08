#include "model/CarModelHandler.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QOpenGLContext>
#include <QVector4D>

#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>

namespace {
}

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

namespace {
    bool hasWheelTag(const QString& name) {
        const QString lowered = name.toLower();
        return lowered.contains("wheel") || lowered.contains("whl") || lowered.contains("rim");
    }

    bool isWheelSubMesh(const QString& objectName, const QString& materialName) {
        const QString combined = (objectName + " " + materialName).toLower();
        if (combined.contains("calip")) {
            return false;
        }
        return hasWheelTag(objectName) || hasWheelTag(materialName);
    }

    struct VertexKey {
        int v = -1;
        int vt = -1;
        int vn = -1;

        bool operator==(const VertexKey& other) const {
            return v == other.v && vt == other.vt && vn == other.vn;
        }
    };

    struct VertexKeyHash {
        size_t operator()(const VertexKey& key) const noexcept {
            const size_t h1 = std::hash<int>{}(key.v);
            const size_t h2 = std::hash<int>{}(key.vt);
            const size_t h3 = std::hash<int>{}(key.vn);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    std::vector<std::string_view> splitWhitespace(std::string_view sv, size_t startIndex) {
        std::vector<std::string_view> tokens;
        size_t i = startIndex;
        while (i < sv.size()) {
            while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t')) {
                ++i;
            }
            if (i >= sv.size()) {
                break;
            }
            size_t j = i;
            while (j < sv.size() && sv[j] != ' ' && sv[j] != '\t') {
                ++j;
            }
            tokens.push_back(sv.substr(i, j - i));
            i = j;
        }
        return tokens;
    }

    bool parseAsciiFloat(std::string_view sv, float& out) {
        const char* first = sv.data();
        const char* last = first + sv.size();
        auto result = std::from_chars(first, last, out);
        if (result.ec == std::errc{} && result.ptr == last) {
            return true;
        }

        std::string tmp(sv);
        char* end = nullptr;
        out = std::strtof(tmp.c_str(), &end);
        return end != nullptr && *end == '\0';
    }

    int resolveObjIndex(int rawIndex, int count) {
        if (rawIndex > 0) {
            return rawIndex - 1;
        }
        if (rawIndex < 0) {
            return count + rawIndex;
        }
        return -1;
    }

    QString resolvePathNearFile(const QFileInfo& baseFile, const QString& relativeOrAbsolutePath) {
        QFileInfo pathInfo(relativeOrAbsolutePath);
        if (pathInfo.isAbsolute()) {
            return canonicalPath(pathInfo.filePath());
        }
        return canonicalPath(baseFile.dir().filePath(relativeOrAbsolutePath));
    }

    QString stripInlineComment(QString line) {
        const qsizetype commentPos = line.indexOf('#');
        if (commentPos >= 0) {
            line.truncate(commentPos);
        }
        return line.trimmed();
    }

    QString parseObjDirectiveValue(std::string_view sv, size_t prefixLength) {
        if (sv.size() <= prefixLength) {
            return {};
        }

        std::string_view value = sv.substr(prefixLength);
        const size_t commentPos = value.find('#');
        if (commentPos != std::string_view::npos) {
            value = value.substr(0, commentPos);
        }

        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())).trimmed();
    }
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

    std::vector<float> tmpPos, tmpNorm, tmpUV, tmpColor;
    std::vector<uint8_t> tmpHasColor;
    std::string line;

    struct FaceCorner {
        int v = 0, vt = -1, vn = -1;
    };

    struct FaceBatch {
        QString objectName;
        QString materialName;
        std::vector<FaceCorner> faces;
    };

    std::map<QString, FaceBatch> faceBatches;
    QString currentMaterial;
    QString currentObject;
    materialLibraryPath_.clear();

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

    qsizetype objLineNumber = 0;
    while (std::getline(stream, line)) {
        ++objLineNumber;
        std::string_view sv(line);
        // trim
        size_t start = 0;
        while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\r')) ++start;
        size_t end = sv.size();
        while (end > start && (sv[end - 1] == ' ' || sv[end - 1] == '\t' || sv[end - 1] == '\r' || sv[end - 1] == '\n')) --end;
        sv = sv.substr(start, end - start);

        if (sv.empty() || sv[0] == '#') continue;

        if (sv.rfind("mtllib ", 0) == 0) {
            const QString mtllibRef = parseObjDirectiveValue(sv, 7);
            if (!mtllibRef.isEmpty()) {
                materialLibraryPath_ = resolvePathNearFile(fi, mtllibRef);
            }
        }
        else if (sv.size() >= 2 && sv[0] == 'v' && sv[1] == ' ') {
            const auto toks = splitWhitespace(sv, 2);
            if (toks.size() >= 3) {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                std::from_chars(toks[0].data(), toks[0].data() + toks[0].size(), x);
                std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), y);
                std::from_chars(toks[2].data(), toks[2].data() + toks[2].size(), z);
                tmpPos.insert(tmpPos.end(), { x, y, z });

                if (toks.size() >= 6) {
                    float r = 1.0f;
                    float g = 1.0f;
                    float b = 1.0f;
                    std::from_chars(toks[3].data(), toks[3].data() + toks[3].size(), r);
                    std::from_chars(toks[4].data(), toks[4].data() + toks[4].size(), g);
                    std::from_chars(toks[5].data(), toks[5].data() + toks[5].size(), b);
                    tmpColor.insert(tmpColor.end(), { r, g, b });
                    tmpHasColor.push_back(1);
                }
                else {
                    tmpColor.insert(tmpColor.end(), { 1.0f, 1.0f, 1.0f });
                    tmpHasColor.push_back(0);
                }
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
        else if (sv.rfind("o ", 0) == 0 || sv.rfind("g ", 0) == 0) {
            currentObject = parseObjDirectiveValue(sv, 2);
        }
        else if (sv.rfind("usemtl ", 0) == 0) {
            currentMaterial = parseObjDirectiveValue(sv, 7);
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
                const QString batchKey = currentObject + "|" + currentMaterial;
                FaceBatch& batch = faceBatches[batchKey];
                batch.objectName = currentObject;
                batch.materialName = currentMaterial;
                // triangulate fan
                for (size_t k = 1; k + 1 < corners.size(); ++k) {
                    batch.faces.push_back(corners[0]);
                    batch.faces.push_back(corners[k]);
                    batch.faces.push_back(corners[k + 1]);
                }
            }
        }
    }
    averageWheelRadius_ = 0.0f;
    float wheelRadiusSum = 0.0f;
    int wheelCount = 0;

    // Build meshes per object+material batch
    for (auto& [batchKey, batch] : faceBatches) {
        Q_UNUSED(batchKey);
        auto& faces = batch.faces;
        if (faces.empty()) continue;

        SubMesh sub;
        sub.objectName = batch.objectName;
        sub.materialName = batch.materialName;

        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;
        auto addVertex = [&](const FaceCorner& fc) -> uint32_t {
            const int v = resolveObjIndex(fc.v, static_cast<int>(tmpPos.size() / 3));
            const int vt = (fc.vt == -1) ? -1 : resolveObjIndex(fc.vt, static_cast<int>(tmpUV.size() / 2));
            const int vn = (fc.vn == -1) ? -1 : resolveObjIndex(fc.vn, static_cast<int>(tmpNorm.size() / 3));

            if (v < 0 || static_cast<size_t>(v) * 3 + 2 >= tmpPos.size()) {
                return std::numeric_limits<uint32_t>::max();
            }

            const VertexKey key{ v, vt, vn };
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
            else {
                sub.normals.push_back(0.0f);
                sub.normals.push_back(1.0f);
                sub.normals.push_back(0.0f);
            }

            if (vt >= 0 && (size_t)vt * 2 + 1 < tmpUV.size()) {
                sub.texcoords.push_back(tmpUV[vt * 2]);
                sub.texcoords.push_back(tmpUV[vt * 2 + 1]);
                sub.hasTexcoords = true;
            }
            else {
                sub.texcoords.push_back(0.0f);
                sub.texcoords.push_back(0.0f);
            }

            if ((size_t)v * 3 + 2 < tmpColor.size()) {
                sub.colors.push_back(tmpColor[v * 3]);
                sub.colors.push_back(tmpColor[v * 3 + 1]);
                sub.colors.push_back(tmpColor[v * 3 + 2]);
                if ((size_t)v < tmpHasColor.size() && tmpHasColor[v] != 0) {
                    sub.hasVertexColors = true;
                }
            }
            else {
                sub.colors.push_back(1.0f);
                sub.colors.push_back(1.0f);
                sub.colors.push_back(1.0f);
            }

            vertexMap[key] = idx;
            return idx;
            };

        for (size_t f = 0; f < faces.size(); f += 3) {
            uint32_t i0 = addVertex(faces[f]);
            uint32_t i1 = addVertex(faces[f + 1]);
            uint32_t i2 = addVertex(faces[f + 2]);
            if (i0 == std::numeric_limits<uint32_t>::max() ||
                i1 == std::numeric_limits<uint32_t>::max() ||
                i2 == std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            sub.indices.push_back(i0);
            sub.indices.push_back(i1);
            sub.indices.push_back(i2);
        }

        if (!sub.positions.empty() && !sub.indices.empty()) {
            sub.isWheel = isWheelSubMesh(sub.objectName, sub.materialName);
            if (sub.isWheel) {
                QVector3D minPos(
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max());
                QVector3D maxPos(
                    -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max(),
                    -std::numeric_limits<float>::max());

                for (size_t i = 0; i + 2 < sub.positions.size(); i += 3) {
                    const QVector3D p(sub.positions[i], sub.positions[i + 1], sub.positions[i + 2]);
                    minPos.setX(std::min(minPos.x(), p.x()));
                    minPos.setY(std::min(minPos.y(), p.y()));
                    minPos.setZ(std::min(minPos.z(), p.z()));
                    maxPos.setX(std::max(maxPos.x(), p.x()));
                    maxPos.setY(std::max(maxPos.y(), p.y()));
                    maxPos.setZ(std::max(maxPos.z(), p.z()));
                }

                sub.localCenter = (minPos + maxPos) * 0.5f;
                float radius = 0.0f;
                for (size_t i = 0; i + 2 < sub.positions.size(); i += 3) {
                    const float dy = sub.positions[i + 1] - sub.localCenter.y();
                    const float dz = sub.positions[i + 2] - sub.localCenter.z();
                    radius = std::max(radius, std::sqrt(dy * dy + dz * dz));
                }
                sub.localWheelRadius = radius;
                if (radius > 1e-5f) {
                    wheelRadiusSum += radius;
                    ++wheelCount;
                }
            }
            meshes_.push_back(std::move(sub));
        }
    }
    if (wheelCount > 0) {
        averageWheelRadius_ = wheelRadiusSum / static_cast<float>(wheelCount);
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
    QString mtlPath = materialLibraryPath_.isEmpty()
        ? canonicalPath(objInfo.path() + "/" + objInfo.baseName() + ".mtl")
        : materialLibraryPath_;
    QFile mtlFile(mtlPath);
    if (!mtlFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open MTL file:" << mtlPath;
        return;
    }

    QString currentMat;
    std::map<QString, QString> textureMap;
    std::map<QString, QVector3D> kdMap;

    qsizetype mtlLineNumber = 0;
    while (!mtlFile.atEnd()) {
        ++mtlLineNumber;
        const QByteArray rawLine = mtlFile.readLine();
        std::string_view sv(rawLine.constData(), static_cast<size_t>(rawLine.size()));
        size_t start = 0;
        while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\r')) {
            ++start;
        }
        size_t end = sv.size();
        while (end > start && (sv[end - 1] == ' ' || sv[end - 1] == '\t' || sv[end - 1] == '\r' || sv[end - 1] == '\n')) {
            --end;
        }
        sv = sv.substr(start, end - start);
        const size_t commentPos = sv.find('#');
        if (commentPos != std::string_view::npos) {
            sv = sv.substr(0, commentPos);
            while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) {
                sv.remove_suffix(1);
            }
        }
        if (sv.empty()) continue;

        if (sv.rfind("newmtl ", 0) == 0) {
            currentMat = parseObjDirectiveValue(sv, 7);
        }
        else if (sv.rfind("Kd ", 0) == 0) {
            const auto parts = splitWhitespace(sv, 3);
            if (parts.size() < 3) {
                continue;
            }

            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            if (!parseAsciiFloat(parts[0], r) || !parseAsciiFloat(parts[1], g) || !parseAsciiFloat(parts[2], b)) {
                continue;
            }
            kdMap[currentMat] = QVector3D(r, g, b);
        }
        else if (sv.rfind("map_Kd ", 0) == 0) {
            QString texPath = parseObjDirectiveValue(sv, 7);
            if (texPath.isEmpty()) {
                continue;
            }
            texPath = resolvePathNearFile(QFileInfo(mtlPath), texPath);
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

    const QString normalized = canonicalPath(path);

    auto cached = textureCache_.find(normalized);
    if (cached != textureCache_.end()) {
        return cached->second;
    }

    QImage img;
    if (!img.load(normalized)) {
        qDebug() << "Failed to load texture:" << normalized;
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
    glBindTexture(GL_TEXTURE_2D, 0);

    textureCache_[normalized] = texId;
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

    if (sub.vao != 0) {
        if (!sub.texturePath.isEmpty() && sub.textureId == 0) {
            sub.textureId = loadTexture(sub.texturePath);
        }
        return;
    }

    // Проверяем, есть ли активный контекст
    if (!QOpenGLContext::currentContext()) {
        qDebug() << "No OpenGL context for uploading submesh!";
        return;
    }

    glGenVertexArrays(1, &sub.vao);
    glGenBuffers(1, &sub.vboPos);
    if (!sub.normals.empty()) glGenBuffers(1, &sub.vboNorm);
    if (!sub.texcoords.empty()) glGenBuffers(1, &sub.vboUV);
    if (!sub.colors.empty()) glGenBuffers(1, &sub.vboColor);
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

    if (!sub.colors.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, sub.vboColor);
        glBufferData(GL_ARRAY_BUFFER,
            sub.colors.size() * sizeof(float),
            sub.colors.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(3);
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
    const QMatrix4x4& viewMatrix,
    float wheelSpinDegrees) {
    if (!glReady_ || meshes_.empty()) return;

    glUseProgram(shader);

    GLint uMVP = glGetUniformLocation(shader, "uMVP");
    GLint uModel = glGetUniformLocation(shader, "uModel");
    GLint uViewPos = glGetUniformLocation(shader, "uViewPos");
    GLint uLightDir = glGetUniformLocation(shader, "uLightDir");
    GLint uUseTexture = glGetUniformLocation(shader, "uUseTexture");
    GLint uTexture = glGetUniformLocation(shader, "uTexture");
    GLint uUseVertexColor = glGetUniformLocation(shader, "uUseVertexColor");
    GLint uColor = glGetUniformLocation(shader, "uColor");  // <-- получаем location цвета

    QVector3D eye = (viewMatrix.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();
    if (uViewPos >= 0) glUniform3f(uViewPos, eye.x(), eye.y(), eye.z());
    if (uLightDir >= 0) glUniform3f(uLightDir, 0.5f, 1.0f, 0.3f);
    glActiveTexture(GL_TEXTURE0);

    const QMatrix4x4 viewProjection = mvp * modelMatrix.inverted();

    // ������������� uColor per-submesh �� Kd (���� �������� ��� � ����� ��������� ���� MTL).

    for (auto& sub : meshes_) {
        if (sub.indexCount == 0) continue;

        QMatrix4x4 subModel = modelMatrix;
        if (sub.isWheel && std::fabs(wheelSpinDegrees) > 1e-4f) {
            subModel.translate(sub.localCenter);
            subModel.rotate(wheelSpinDegrees, 1.0f, 0.0f, 0.0f);
            subModel.translate(-sub.localCenter);
        }
        const QMatrix4x4 subMvp = viewProjection * subModel;

        if (uMVP >= 0) glUniformMatrix4fv(uMVP, 1, GL_FALSE, subMvp.constData());
        if (uModel >= 0) glUniformMatrix4fv(uModel, 1, GL_FALSE, subModel.constData());

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

        if (uUseVertexColor >= 0) {
            const bool useVertexColor =
                sub.hasVertexColors &&
                sub.textureId == 0 &&
                sub.kdColor.x() >= 0.99f &&
                sub.kdColor.y() >= 0.99f &&
                sub.kdColor.z() >= 0.99f;
            glUniform1i(uUseVertexColor, useVertexColor ? 1 : 0);
        }

        if (uUseTexture >= 0) {
            // Используем текстуры, если они есть
            // �����: �������� ������ �������� ��� UV.
            // ���� vboUV �� ������ (��� texcoords), vUV � ��������� ������� ����� �����������������,
            // � ����� �������� ����� ��������� ��� "���������".
            // ������������� �� ������� texcoords � �������� ���������.
            // ��� �������, ��� ��������� vboUV (�� ������� �� ����, ����� �� ����������� �����).
            int useTex = (sub.textureId != 0 && sub.hasTexcoords) ? 1 : 0;
            glUniform1i(uUseTexture, useTex);

            if (useTex && uTexture >= 0) {
                glBindTexture(GL_TEXTURE_2D, sub.textureId);
                glUniform1i(uTexture, 0);
            }
            else {
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        glBindVertexArray(sub.vao);
        glDrawElements(GL_TRIANGLES, sub.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CarModelHandler::clear() {
    clearGPUResources();
    meshes_.clear();
    path_.clear();
    materialLibraryPath_.clear();
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
            if (sub.vboColor) glDeleteBuffers(1, &sub.vboColor);
            if (sub.ibo) glDeleteBuffers(1, &sub.ibo);
        }
        sub = SubMesh{};
    }

    if (hasContext) {
        for (auto& [path, tex] : textureCache_) {
            Q_UNUSED(path);
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
        if (sub.vboColor) glDeleteBuffers(1, &sub.vboColor);
        if (sub.ibo) glDeleteBuffers(1, &sub.ibo);
    }
    sub = SubMesh{};
}
