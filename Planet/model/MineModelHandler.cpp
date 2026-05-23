#include "model/MineModelHandler.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QOpenGLContext>
#include <QRegularExpression>
#include <QVector4D>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>

MineModelHandler::~MineModelHandler() {
    clearGPUResources();
}

static QString canonicalMinePath(const QString& path) {
    QFileInfo fi(path);
    if (fi.exists()) {
        return fi.absoluteFilePath();
    }
    return path;
}

namespace {
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

    struct FaceCorner {
        int v = 0;
        int vt = -1;
        int vn = -1;
    };

    struct FaceBatch {
        QString objectName;
        QString materialName;
        std::vector<FaceCorner> faces;
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
            return canonicalMinePath(pathInfo.filePath());
        }
        return canonicalMinePath(baseFile.dir().filePath(relativeOrAbsolutePath));
    }
}

void MineModelHandler::resetDerivedPlacementData() {
    localPlacement_.setToIdentity();
}

void MineModelHandler::finalizePlacement() {
    resetDerivedPlacementData();

    if (meshes_.empty()) {
        return;
    }

    QVector3D minPos(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    QVector3D maxPos(
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max());

    for (const auto& sub : meshes_) {
        for (size_t i = 0; i + 2 < sub.positions.size(); i += 3) {
            const QVector3D p(sub.positions[i], sub.positions[i + 1], sub.positions[i + 2]);
            minPos.setX(std::min(minPos.x(), p.x()));
            minPos.setY(std::min(minPos.y(), p.y()));
            minPos.setZ(std::min(minPos.z(), p.z()));
            maxPos.setX(std::max(maxPos.x(), p.x()));
            maxPos.setY(std::max(maxPos.y(), p.y()));
            maxPos.setZ(std::max(maxPos.z(), p.z()));
        }
    }

    const QVector3D horizontalCenter(
        (minPos.x() + maxPos.x()) * 0.5f,
        0.0f,
        (minPos.z() + maxPos.z()) * 0.5f);

    const float localGroundY = minPos.y();
    localPlacement_.translate(-horizontalCenter.x(), -localGroundY, -horizontalCenter.z());
}

void MineModelHandler::finalizeCartAnimation() {
    if (meshes_.empty()) {
        return;
    }

    struct Bounds {
        QVector3D minPos;
        QVector3D maxPos;
        QVector3D size;
        QVector3D center;
    };

    auto boundsFor = [](const SubMesh& sub) -> Bounds {
        Bounds bounds{
            QVector3D(
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()),
            QVector3D(
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max()),
            QVector3D(),
            QVector3D()
        };

        for (size_t i = 0; i + 2 < sub.positions.size(); i += 3) {
            const QVector3D p(sub.positions[i], sub.positions[i + 1], sub.positions[i + 2]);
            bounds.minPos.setX(std::min(bounds.minPos.x(), p.x()));
            bounds.minPos.setY(std::min(bounds.minPos.y(), p.y()));
            bounds.minPos.setZ(std::min(bounds.minPos.z(), p.z()));
            bounds.maxPos.setX(std::max(bounds.maxPos.x(), p.x()));
            bounds.maxPos.setY(std::max(bounds.maxPos.y(), p.y()));
            bounds.maxPos.setZ(std::max(bounds.maxPos.z(), p.z()));
        }

        bounds.size = bounds.maxPos - bounds.minPos;
        bounds.center = (bounds.minPos + bounds.maxPos) * 0.5f;
        return bounds;
    };

    struct Candidate {
        int index = -1;
        Bounds bounds;
        float score = std::numeric_limits<float>::max();
    };

    for (auto& sub : meshes_) {
        sub.isCart = false;
        sub.cartTravelAxis = QVector3D(0.0f, 0.0f, 0.0f);
        sub.cartTravelDistance = 0.0f;

        const Bounds bounds = boundsFor(sub);
        sub.localCenter = bounds.center;
    }

    QVector3D globalMin(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max());
    QVector3D globalMax(
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max());

    for (const auto& sub : meshes_) {
        const Bounds bounds = boundsFor(sub);
        globalMin.setX(std::min(globalMin.x(), bounds.minPos.x()));
        globalMin.setY(std::min(globalMin.y(), bounds.minPos.y()));
        globalMin.setZ(std::min(globalMin.z(), bounds.minPos.z()));
        globalMax.setX(std::max(globalMax.x(), bounds.maxPos.x()));
        globalMax.setY(std::max(globalMax.y(), bounds.maxPos.y()));
        globalMax.setZ(std::max(globalMax.z(), bounds.maxPos.z()));
    }

    const QVector3D globalCenter = (globalMin + globalMax) * 0.5f;
    constexpr float kCartTravelDistance = 1.15f;
    Candidate bodyCandidate;
    Candidate baseCandidate;

    auto centerPenalty = [](const QVector3D& center, float expectedX, float expectedZ) {
        return std::abs(center.x() - expectedX) + std::abs(center.z() - expectedZ);
    };

    for (int i = 0; i < static_cast<int>(meshes_.size()); ++i) {
        const auto& sub = meshes_[i];
        if (sub.objectName != "obj2") {
            continue;
        }

        const Bounds bounds = boundsFor(sub);
        const bool entranceZone =
            bounds.center.x() >= 11.65f && bounds.center.x() <= 12.05f &&
            bounds.center.z() >= -1.20f && bounds.center.z() <= -0.70f &&
            bounds.minPos.y() >= -0.10f && bounds.maxPos.y() <= 1.30f;
        if (!entranceZone) {
            continue;
        }

        const float zonePenalty = centerPenalty(bounds.center, 11.855f, -0.94f);

        const bool cartBody =
            bounds.size.x() >= 5.5f && bounds.size.x() <= 6.0f &&
            bounds.size.y() >= 1.05f && bounds.size.y() <= 1.30f &&
            bounds.size.z() >= 2.80f && bounds.size.z() <= 3.05f;
        if (cartBody) {
            const float sizePenalty =
                std::abs(bounds.size.x() - 2.42f) +
                std::abs(bounds.size.y() - 1.21f) +
                std::abs(bounds.size.z() - 2.95f);
            const float score = zonePenalty + sizePenalty;
            if (score < bodyCandidate.score) {
                bodyCandidate = Candidate{ i, bounds, score };
            }
        }

        const bool cartBase =
            bounds.size.x() >= 2.20f && bounds.size.x() <= 2.40f &&
            bounds.size.y() >= 0.05f && bounds.size.y() <= 0.25f &&
            bounds.size.z() >= 2.65f && bounds.size.z() <= 2.90f;
        if (cartBase) {
            const float sizePenalty =
                std::abs(bounds.size.x() - 2.31f) +
                std::abs(bounds.size.y() - 0.14f) +
                std::abs(bounds.size.z() - 2.78f);
            const float score = zonePenalty + sizePenalty;
            if (score < baseCandidate.score) {
                baseCandidate = Candidate{ i, bounds, score };
            }
        }
    }

    if (bodyCandidate.index < 0 && baseCandidate.index < 0) {
        return;
    }

    QVector3D cartCenter(0.0f, 0.0f, 0.0f);
    int centerContributors = 0;
    if (bodyCandidate.index >= 0) {
        cartCenter += bodyCandidate.bounds.center;
        ++centerContributors;
    }
    if (baseCandidate.index >= 0) {
        cartCenter += baseCandidate.bounds.center;
        ++centerContributors;
    }
    cartCenter /= static_cast<float>(std::max(centerContributors, 1));

    QVector3D inward = globalCenter - cartCenter;
    inward.setY(0.0f);
    if (inward.lengthSquared() > 1e-6f) {
        inward.normalize();
    }
    else {
        inward = QVector3D(-1.0f, 0.0f, 0.0f);
    }

    if (bodyCandidate.index >= 0) {
        auto& sub = meshes_[bodyCandidate.index];
        sub.isCart = true;
        sub.cartTravelAxis = inward;
        sub.cartTravelDistance = kCartTravelDistance;
    }
    if (baseCandidate.index >= 0) {
        auto& sub = meshes_[baseCandidate.index];
        sub.isCart = true;
        sub.cartTravelAxis = inward;
        sub.cartTravelDistance = kCartTravelDistance;
    }
}

bool MineModelHandler::loadFromFile(const QString& path) {
    const QString normalized = canonicalMinePath(path);

    if (path_ == normalized && !meshes_.empty()) {
        return true;
    }

    if (!path_.isEmpty() && path_ != normalized) {
        clear();
    }

    qDebug() << "Loading mine model:" << normalized;
    resetDerivedPlacementData();

    QFileInfo fi(normalized);
    if (fi.suffix().toLower() != "obj") {
        qDebug() << "MineModelHandler only supports OBJ format";
        return false;
    }

    QFile file(normalized);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open file:" << normalized;
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();

    std::istringstream stream(std::string(raw.constData(), raw.size()));
    std::vector<float> tmpPos;
    std::vector<float> tmpNorm;
    std::vector<float> tmpUV;
    std::vector<float> tmpColor;
    std::vector<uint8_t> tmpHasColor;
    std::unordered_map<QString, FaceBatch> faceBatches;
    QString currentMaterial;
    QString currentObject;
    materialLibraryPath_.clear();
    std::string line;

    auto parseFaceCorner = [](const std::string_view& tok) -> FaceCorner {
        FaceCorner fc;
        int slash1 = -1;
        int slash2 = -1;
        for (size_t i = 0; i < tok.size(); ++i) {
            if (tok[i] == '/') {
                if (slash1 < 0) {
                    slash1 = static_cast<int>(i);
                }
                else {
                    slash2 = static_cast<int>(i);
                    break;
                }
            }
        }

        auto parseInt = [](std::string_view sv, int& value) -> bool {
            const char* first = sv.data();
            const char* last = first + sv.size();
            const auto result = std::from_chars(first, last, value);
            return result.ec == std::errc{};
        };

        if (slash1 < 0) {
            parseInt(tok, fc.v);
        }
        else if (slash2 < 0) {
            parseInt(tok.substr(0, slash1), fc.v);
            const std::string_view vt = tok.substr(slash1 + 1);
            if (!vt.empty()) {
                parseInt(vt, fc.vt);
            }
        }
        else {
            parseInt(tok.substr(0, slash1), fc.v);
            const std::string_view vt = tok.substr(slash1 + 1, slash2 - slash1 - 1);
            if (!vt.empty()) {
                parseInt(vt, fc.vt);
            }
            const std::string_view vn = tok.substr(slash2 + 1);
            if (!vn.empty()) {
                parseInt(vn, fc.vn);
            }
        }

        return fc;
    };

    while (std::getline(stream, line)) {
        std::string_view sv(line);
        size_t start = 0;
        while (start < sv.size() && (sv[start] == ' ' || sv[start] == '\t' || sv[start] == '\r')) {
            ++start;
        }
        size_t end = sv.size();
        while (end > start && (sv[end - 1] == ' ' || sv[end - 1] == '\t' || sv[end - 1] == '\r' || sv[end - 1] == '\n')) {
            --end;
        }
        sv = sv.substr(start, end - start);

        if (sv.empty() || sv[0] == '#') {
            continue;
        }

        if (sv.rfind("mtllib ", 0) == 0) {
            const QString mtllibRef = QString::fromStdString(std::string(sv.substr(7))).trimmed();
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
            const auto toks = splitWhitespace(sv, 3);
            if (toks.size() >= 2) {
                float u = 0.0f;
                float v = 0.0f;
                std::from_chars(toks[0].data(), toks[0].data() + toks[0].size(), u);
                std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), v);
                tmpUV.insert(tmpUV.end(), { u, v });
            }
        }
        else if (sv.size() >= 3 && sv[0] == 'v' && sv[1] == 'n' && sv[2] == ' ') {
            const auto toks = splitWhitespace(sv, 3);
            if (toks.size() >= 3) {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                std::from_chars(toks[0].data(), toks[0].data() + toks[0].size(), x);
                std::from_chars(toks[1].data(), toks[1].data() + toks[1].size(), y);
                std::from_chars(toks[2].data(), toks[2].data() + toks[2].size(), z);
                tmpNorm.insert(tmpNorm.end(), { x, y, z });
            }
        }
        else if (sv.rfind("o ", 0) == 0 || sv.rfind("g ", 0) == 0) {
            currentObject = QString::fromStdString(std::string(sv.substr(2))).trimmed();
        }
        else if (sv.rfind("usemtl ", 0) == 0) {
            currentMaterial = QString::fromStdString(std::string(sv.substr(7))).trimmed();
        }
        else if (sv.size() >= 2 && sv[0] == 'f' && sv[1] == ' ') {
            const auto toks = splitWhitespace(sv, 2);
            if (toks.size() >= 3) {
                std::vector<FaceCorner> corners;
                corners.reserve(toks.size());
                for (const auto tok : toks) {
                    corners.push_back(parseFaceCorner(tok));
                }

                const QString batchKey = currentObject + "|" + currentMaterial;
                FaceBatch& batch = faceBatches[batchKey];
                batch.objectName = currentObject;
                batch.materialName = currentMaterial;

                for (size_t k = 1; k + 1 < corners.size(); ++k) {
                    batch.faces.push_back(corners[0]);
                    batch.faces.push_back(corners[k]);
                    batch.faces.push_back(corners[k + 1]);
                }
            }
        }
    }

    for (auto& [batchKey, batch] : faceBatches) {
        Q_UNUSED(batchKey);
        if (batch.faces.empty()) {
            continue;
        }

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
            const auto it = vertexMap.find(key);
            if (it != vertexMap.end()) {
                return it->second;
            }

            const uint32_t idx = static_cast<uint32_t>(sub.positions.size() / 3);
            sub.positions.push_back(tmpPos[v * 3]);
            sub.positions.push_back(tmpPos[v * 3 + 1]);
            sub.positions.push_back(tmpPos[v * 3 + 2]);

            if (vn >= 0 && static_cast<size_t>(vn) * 3 + 2 < tmpNorm.size()) {
                sub.normals.push_back(tmpNorm[vn * 3]);
                sub.normals.push_back(tmpNorm[vn * 3 + 1]);
                sub.normals.push_back(tmpNorm[vn * 3 + 2]);
            }
            else {
                sub.normals.push_back(0.0f);
                sub.normals.push_back(1.0f);
                sub.normals.push_back(0.0f);
            }

            if (vt >= 0 && static_cast<size_t>(vt) * 2 + 1 < tmpUV.size()) {
                sub.texcoords.push_back(tmpUV[vt * 2]);
                sub.texcoords.push_back(tmpUV[vt * 2 + 1]);
                sub.hasTexcoords = true;
            }
            else {
                sub.texcoords.push_back(0.0f);
                sub.texcoords.push_back(0.0f);
            }

            if (static_cast<size_t>(v) * 3 + 2 < tmpColor.size()) {
                sub.colors.push_back(tmpColor[v * 3]);
                sub.colors.push_back(tmpColor[v * 3 + 1]);
                sub.colors.push_back(tmpColor[v * 3 + 2]);
                if (static_cast<size_t>(v) < tmpHasColor.size() && tmpHasColor[v] != 0) {
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

        for (size_t f = 0; f < batch.faces.size(); f += 3) {
            const uint32_t i0 = addVertex(batch.faces[f]);
            const uint32_t i1 = addVertex(batch.faces[f + 1]);
            const uint32_t i2 = addVertex(batch.faces[f + 2]);
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
            const size_t vertexCount = sub.positions.size() / 3;
            const size_t triangleCount = sub.indices.size() / 3;

            std::vector<std::vector<uint32_t>> vertexToTriangles(vertexCount);
            for (uint32_t tri = 0; tri < static_cast<uint32_t>(triangleCount); ++tri) {
                const uint32_t i0 = sub.indices[tri * 3 + 0];
                const uint32_t i1 = sub.indices[tri * 3 + 1];
                const uint32_t i2 = sub.indices[tri * 3 + 2];
                if (i0 < vertexCount) vertexToTriangles[i0].push_back(tri);
                if (i1 < vertexCount) vertexToTriangles[i1].push_back(tri);
                if (i2 < vertexCount) vertexToTriangles[i2].push_back(tri);
            }

            std::vector<uint8_t> visited(triangleCount, 0);
            std::vector<uint32_t> stack;

            for (uint32_t startTri = 0; startTri < static_cast<uint32_t>(triangleCount); ++startTri) {
                if (visited[startTri]) {
                    continue;
                }

                SubMesh component;
                component.objectName = sub.objectName;
                component.materialName = sub.materialName;
                component.hasTexcoords = sub.hasTexcoords;
                component.hasVertexColors = sub.hasVertexColors;

                std::unordered_map<uint32_t, uint32_t> remap;
                stack.clear();
                stack.push_back(startTri);
                visited[startTri] = 1;

                while (!stack.empty()) {
                    const uint32_t tri = stack.back();
                    stack.pop_back();

                    const uint32_t triIndices[3] = {
                        sub.indices[tri * 3 + 0],
                        sub.indices[tri * 3 + 1],
                        sub.indices[tri * 3 + 2]
                    };

                    for (int corner = 0; corner < 3; ++corner) {
                        const uint32_t oldIndex = triIndices[corner];
                        auto it = remap.find(oldIndex);
                        uint32_t newIndex = 0;
                        if (it == remap.end()) {
                            newIndex = static_cast<uint32_t>(component.positions.size() / 3);
                            remap.emplace(oldIndex, newIndex);

                            component.positions.push_back(sub.positions[oldIndex * 3 + 0]);
                            component.positions.push_back(sub.positions[oldIndex * 3 + 1]);
                            component.positions.push_back(sub.positions[oldIndex * 3 + 2]);

                            if (!sub.normals.empty()) {
                                component.normals.push_back(sub.normals[oldIndex * 3 + 0]);
                                component.normals.push_back(sub.normals[oldIndex * 3 + 1]);
                                component.normals.push_back(sub.normals[oldIndex * 3 + 2]);
                            }
                            if (!sub.texcoords.empty()) {
                                component.texcoords.push_back(sub.texcoords[oldIndex * 2 + 0]);
                                component.texcoords.push_back(sub.texcoords[oldIndex * 2 + 1]);
                            }
                            if (!sub.colors.empty()) {
                                component.colors.push_back(sub.colors[oldIndex * 3 + 0]);
                                component.colors.push_back(sub.colors[oldIndex * 3 + 1]);
                                component.colors.push_back(sub.colors[oldIndex * 3 + 2]);
                            }
                        }
                        else {
                            newIndex = it->second;
                        }

                        component.indices.push_back(newIndex);

                        for (const uint32_t neighborTri : vertexToTriangles[oldIndex]) {
                            if (!visited[neighborTri]) {
                                visited[neighborTri] = 1;
                                stack.push_back(neighborTri);
                            }
                        }
                    }
                }

                if (!component.positions.empty() && !component.indices.empty()) {
                    meshes_.push_back(std::move(component));
                }
            }
        }
    }

    if (meshes_.empty()) {
        qDebug() << "No mesh data loaded for mine model";
        return false;
    }

    finalizePlacement();
    finalizeCartAnimation();
    path_ = normalized;
    loadMaterials(normalized);
    return true;
}

void MineModelHandler::loadMaterials(const QString& objPath) {
    QFileInfo objInfo(objPath);
    const QString mtlPath = materialLibraryPath_.isEmpty()
        ? canonicalMinePath(objInfo.path() + "/" + objInfo.baseName() + ".mtl")
        : materialLibraryPath_;

    QFile mtlFile(mtlPath);
    if (!mtlFile.open(QIODevice::ReadOnly)) {
        qDebug() << "Cannot open MTL file:" << mtlPath;
        return;
    }

    struct MaterialInfo {
        QString diffuseTexturePath;
        QString metallicRoughnessPath;
        QString normalTexturePath;
        QVector3D kdColor{ 1.0f, 1.0f, 1.0f };
    };

    QString currentMat;
    std::map<QString, MaterialInfo> materialMap;
    const QRegularExpression wsRe(QStringLiteral("\\s+"));

    while (!mtlFile.atEnd()) {
        const QString line = QString::fromUtf8(mtlFile.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        const QStringList parts = line.split(wsRe, Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            continue;
        }

        if (parts[0] == "newmtl") {
            currentMat = line.mid(7).trimmed();
        }
        else if (parts[0] == "Kd" && parts.size() >= 4) {
            materialMap[currentMat].kdColor = QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat());
        }
        else if (parts[0] == "map_Kd" && parts.size() >= 2) {
            materialMap[currentMat].diffuseTexturePath =
                resolvePathNearFile(QFileInfo(mtlPath), line.mid(QStringLiteral("map_Kd").size()).trimmed());
        }
        else if ((parts[0] == "map_Ns" || parts[0] == "refl") && parts.size() >= 2) {
            materialMap[currentMat].metallicRoughnessPath =
                resolvePathNearFile(QFileInfo(mtlPath), line.mid(parts[0].size()).trimmed());
        }
        else if ((parts[0] == "bump" || parts[0] == "map_Bump" || parts[0] == "norm") && parts.size() >= 2) {
            materialMap[currentMat].normalTexturePath =
                resolvePathNearFile(QFileInfo(mtlPath), line.mid(parts[0].size()).trimmed());
        }
    }
    mtlFile.close();

    for (auto& sub : meshes_) {
        const auto it = materialMap.find(sub.materialName);
        if (it == materialMap.end()) {
            continue;
        }

        sub.diffuseTexturePath = it->second.diffuseTexturePath;
        sub.metallicRoughnessPath = it->second.metallicRoughnessPath;
        sub.normalTexturePath = it->second.normalTexturePath;
        sub.kdColor = it->second.kdColor;
    }
}

GLuint MineModelHandler::loadTexture(const QString& path) {
    if (!QOpenGLContext::currentContext()) {
        qDebug() << "Cannot load texture" << path << "- no OpenGL context";
        return 0;
    }

    const QString normalized = canonicalMinePath(path);
    const auto cached = textureCache_.find(normalized);
    if (cached != textureCache_.end()) {
        return cached->second;
    }

    QImage img;
    if (!img.load(normalized)) {
        qDebug() << "Failed to load texture:" << normalized;
        return 0;
    }

    // The mine atlas comes from a toolchain with bottom-left UV origin,
    // so we flip the image once on upload to keep wagon/rails sampling correct.
    img = img.mirrored(false, true).convertToFormat(QImage::Format_RGBA8888);

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    textureCache_[normalized] = texId;
    return texId;
}

void MineModelHandler::uploadToGPU() {
    if (!QOpenGLContext::currentContext()) {
        return;
    }
    if (!glReady_) {
        initializeOpenGLFunctions();
        glReady_ = true;
    }

    for (auto& sub : meshes_) {
        uploadSubMeshToGPU(sub);
    }
}

void MineModelHandler::uploadSubMeshToGPU(SubMesh& sub) {
    if (sub.positions.empty()) {
        return;
    }

    if (sub.vao != 0) {
        if (!sub.diffuseTexturePath.isEmpty() && sub.textureId == 0) {
            sub.textureId = loadTexture(sub.diffuseTexturePath);
        }
        return;
    }

    if (!QOpenGLContext::currentContext()) {
        qDebug() << "No OpenGL context for uploading mine submesh";
        return;
    }

    glGenVertexArrays(1, &sub.vao);
    glGenBuffers(1, &sub.vboPos);
    if (!sub.normals.empty()) {
        glGenBuffers(1, &sub.vboNorm);
    }
    if (!sub.texcoords.empty()) {
        glGenBuffers(1, &sub.vboUV);
    }
    if (!sub.colors.empty()) {
        glGenBuffers(1, &sub.vboColor);
    }
    glGenBuffers(1, &sub.ibo);

    glBindVertexArray(sub.vao);

    glBindBuffer(GL_ARRAY_BUFFER, sub.vboPos);
    glBufferData(GL_ARRAY_BUFFER, sub.positions.size() * sizeof(float), sub.positions.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    if (!sub.normals.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, sub.vboNorm);
        glBufferData(GL_ARRAY_BUFFER, sub.normals.size() * sizeof(float), sub.normals.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(1);
    }

    if (!sub.texcoords.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, sub.vboUV);
        glBufferData(GL_ARRAY_BUFFER, sub.texcoords.size() * sizeof(float), sub.texcoords.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(2);
    }

    if (!sub.colors.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, sub.vboColor);
        glBufferData(GL_ARRAY_BUFFER, sub.colors.size() * sizeof(float), sub.colors.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(3);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sub.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sub.indices.size() * sizeof(uint32_t), sub.indices.data(), GL_STATIC_DRAW);

    sub.indexCount = static_cast<GLsizei>(sub.indices.size());
    glBindVertexArray(0);

    if (!sub.diffuseTexturePath.isEmpty() && sub.textureId == 0) {
        sub.textureId = loadTexture(sub.diffuseTexturePath);
    }
}

void MineModelHandler::draw(GLuint shader,
    const QMatrix4x4& mvp,
    const QMatrix4x4& modelMatrix,
    const QMatrix4x4& viewMatrix) {
    if (!glReady_ || meshes_.empty()) {
        return;
    }

    glUseProgram(shader);

    const GLint uMVP = glGetUniformLocation(shader, "uMVP");
    const GLint uModel = glGetUniformLocation(shader, "uModel");
    const GLint uViewPos = glGetUniformLocation(shader, "uViewPos");
    const GLint uLightDir = glGetUniformLocation(shader, "uLightDir");
    const GLint uUseTexture = glGetUniformLocation(shader, "uUseTexture");
    const GLint uTexture = glGetUniformLocation(shader, "uTexture");
    const GLint uUseVertexColor = glGetUniformLocation(shader, "uUseVertexColor");
    const GLint uColor = glGetUniformLocation(shader, "uColor");

    const QVector3D eye = (viewMatrix.inverted() * QVector4D(0, 0, 0, 1)).toVector3D();
    if (uViewPos >= 0) {
        glUniform3f(uViewPos, eye.x(), eye.y(), eye.z());
    }
    if (uLightDir >= 0) {
        glUniform3f(uLightDir, 0.35f, 1.0f, 0.2f);
    }
    glActiveTexture(GL_TEXTURE0);

    const QMatrix4x4 viewProjection = mvp * modelMatrix.inverted();
    if (!cartAnimationTimer_.isValid()) {
        cartAnimationTimer_.start();
    }
    const float timeSeconds = static_cast<float>(cartAnimationTimer_.elapsed()) / 1000.0f;

    for (auto& sub : meshes_) {
        if (sub.indexCount == 0) {
            continue;
        }

        QMatrix4x4 subModel = modelMatrix;
        if (sub.isCart && sub.cartTravelDistance > 1e-4f && sub.cartTravelAxis.lengthSquared() > 1e-6f) {
            constexpr float kCartCycleSeconds = 4.6f;
            constexpr float kTwoPi = 6.28318530718f;
            const float phase = std::fmod(timeSeconds / kCartCycleSeconds, 1.0f);
            const float pingPong = 0.5f - 0.5f * std::cos(phase * kTwoPi);
            const QVector3D offset = sub.cartTravelAxis * (pingPong * sub.cartTravelDistance);
            subModel.translate(offset);
        }

        const QMatrix4x4 subMvp = viewProjection * subModel;
        if (uMVP >= 0) {
            glUniformMatrix4fv(uMVP, 1, GL_FALSE, subMvp.constData());
        }
        if (uModel >= 0) {
            glUniformMatrix4fv(uModel, 1, GL_FALSE, subModel.constData());
        }

        if (sub.textureId == 0 && !sub.diffuseTexturePath.isEmpty()) {
            sub.textureId = loadTexture(sub.diffuseTexturePath);
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
            const int useTex = (sub.textureId != 0 && sub.hasTexcoords) ? 1 : 0;
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

void MineModelHandler::clear() {
    clearGPUResources();
    meshes_.clear();
    path_.clear();
    materialLibraryPath_.clear();
    resetDerivedPlacementData();
}

void MineModelHandler::clearGPUResources() {
    const bool hasContext = QOpenGLContext::currentContext() != nullptr;

    for (auto& sub : meshes_) {
        if (hasContext && sub.vao) {
            glDeleteVertexArrays(1, &sub.vao);
            glDeleteBuffers(1, &sub.vboPos);
            if (sub.vboNorm) {
                glDeleteBuffers(1, &sub.vboNorm);
            }
            if (sub.vboUV) {
                glDeleteBuffers(1, &sub.vboUV);
            }
            if (sub.vboColor) {
                glDeleteBuffers(1, &sub.vboColor);
            }
            if (sub.ibo) {
                glDeleteBuffers(1, &sub.ibo);
            }
        }
        sub = SubMesh{};
    }

    if (hasContext) {
        for (auto& [path, tex] : textureCache_) {
            Q_UNUSED(path);
            if (tex) {
                glDeleteTextures(1, &tex);
            }
        }
    }
    textureCache_.clear();

    glReady_ = false;
}
