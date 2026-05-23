#include "contributor/ContributorAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <QDebug>
#include <random>
#include <vector>
#include "contributor/ContributorParticles.h"

namespace {

    struct Vec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Vec3 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3() = default;
        Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

        Vec3 operator+(const Vec3& r) const { return { x + r.x, y + r.y, z + r.z }; }
        Vec3 operator-(const Vec3& r) const { return { x - r.x, y - r.y, z - r.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
        Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }

        Vec3& operator+=(const Vec3& r) {
            x += r.x;
            y += r.y;
            z += r.z;
            return *this;
        }
    };

    struct Vertex {
        Vec3 pos;
        Vec3 normal;
        Vec2 uv;
    };

    struct Mesh {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    struct TreeMeshes {
        Mesh wood;
        Mesh leaves;
        std::vector<Vec3> branchTips;  // ← добавить эту строку
    };

    struct TreeParams {
        int trunkSegments = 14;
        int radialSegments = 10;
        float trunkHeight = 6.0f;
        float trunkRadiusBase = 0.35f;
        float trunkRadiusTop = 0.10f;
        int branchCount = 7;
        int branchSegments = 5;
        float branchLengthMin = 1.0f;
        float branchLengthMax = 2.1f;
        float branchRadiusFactor = 0.38f;
        float branchUpBias = 0.45f;
        float trunkBend = 0.7f;
        float trunkNoise = 0.25f;
        int leafBlobSubdivLat = 6;
        int leafBlobSubdivLon = 8;
        float leafBlobRadiusMin = 0.45f;
        float leafBlobRadiusMax = 0.90f;
        int leafBlobsPerBranch = 2;
        uint32_t seed = 1337;
    };

    class RNG {
    public:
        explicit RNG(uint32_t seed) : eng_(seed) {}

        float uniform(float a, float b) {
            return std::uniform_real_distribution<float>(a, b)(eng_);
        }

    private:
        std::mt19937 eng_;
    };

    float dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 cross(const Vec3& a, const Vec3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    float length(const Vec3& v) {
        return std::sqrt(dot(v, v));
    }

    Vec3 normalize(const Vec3& v) {
        const float len = length(v);
        if (len < 1e-8f) {
            return { 0.0f, 1.0f, 0.0f };
        }
        return v / len;
    }

    float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
        return a * (1.0f - t) + b * t;
    }

    Vec3 orthogonalUp(const Vec3& tangent) {
        const Vec3 up = (std::fabs(tangent.y) < 0.92f) ? Vec3{ 0.0f, 1.0f, 0.0f } : Vec3{ 1.0f, 0.0f, 0.0f };
        Vec3 right = normalize(cross(up, tangent));
        if (length(right) < 1e-6f) {
            right = { 1.0f, 0.0f, 0.0f };
        }
        return normalize(cross(tangent, right));
    }

    void appendTube(Mesh& mesh, const std::vector<Vec3>& points, const std::vector<float>& radii, int radialSegments) {
        if (points.size() < 2 || points.size() != radii.size()) {
            return;
        }

        const uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

        std::vector<Vec3> tangents(points.size());
        for (size_t i = 0; i < points.size(); ++i) {
            if (i == 0) {
                tangents[i] = normalize(points[1] - points[0]);
            }
            else if (i + 1 == points.size()) {
                tangents[i] = normalize(points[i] - points[i - 1]);
            }
            else {
                tangents[i] = normalize(points[i + 1] - points[i - 1]);
            }
        }

        for (size_t i = 0; i < points.size(); ++i) {
            const Vec3 t = tangents[i];
            const Vec3 n = orthogonalUp(t);
            const Vec3 b = normalize(cross(t, n));
            const float v = static_cast<float>(i) / static_cast<float>(points.size() - 1);

            for (int j = 0; j < radialSegments; ++j) {
                const float u = static_cast<float>(j) / static_cast<float>(radialSegments);
                const float a = 2.0f * 3.1415926535f * u;
                const Vec3 radial = n * std::cos(a) + b * std::sin(a);
                mesh.vertices.push_back({ points[i] + radial * radii[i], normalize(radial), { u, v } });
            }
        }

        const int rings = static_cast<int>(points.size());
        for (int i = 0; i < rings - 1; ++i) {
            for (int j = 0; j < radialSegments; ++j) {
                const int j1 = (j + 1) % radialSegments;
                const uint32_t i0 = baseVertex + i * radialSegments + j;
                const uint32_t i1 = baseVertex + i * radialSegments + j1;
                const uint32_t i2 = baseVertex + (i + 1) * radialSegments + j;
                const uint32_t i3 = baseVertex + (i + 1) * radialSegments + j1;
                mesh.indices.insert(mesh.indices.end(), { i0, i2, i1, i1, i2, i3 });
            }
        }
    }

    void appendSphereBlob(Mesh& mesh, const Vec3& center, float radius, int latSteps, int lonSteps) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());

        for (int iy = 0; iy <= latSteps; ++iy) {
            const float v = static_cast<float>(iy) / static_cast<float>(latSteps);
            const float phi = v * 3.1415926535f;

            for (int ix = 0; ix <= lonSteps; ++ix) {
                const float u = static_cast<float>(ix) / static_cast<float>(lonSteps);
                const float theta = u * 2.0f * 3.1415926535f;
                const Vec3 n = {
                    std::sin(phi) * std::cos(theta),
                    std::cos(phi),
                    std::sin(phi) * std::sin(theta)
                };
                const Vec3 p = {
                    center.x + n.x * radius,
                    center.y + n.y * radius * 0.75f,
                    center.z + n.z * radius
                };
                mesh.vertices.push_back({ p, normalize(n), { u, v } });
            }
        }

        for (int iy = 0; iy < latSteps; ++iy) {
            for (int ix = 0; ix < lonSteps; ++ix) {
                const uint32_t i0 = base + iy * (lonSteps + 1) + ix;
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = i0 + (lonSteps + 1);
                const uint32_t i3 = i2 + 1;
                mesh.indices.insert(mesh.indices.end(), { i0, i2, i1, i1, i2, i3 });
            }
        }
    }

    std::vector<Vec3> makeTrunkCurve(const TreeParams& p, RNG& rng) {
        std::vector<Vec3> curve;
        curve.reserve(p.trunkSegments + 1);

        const Vec3 drift{ rng.uniform(-p.trunkBend, p.trunkBend), 0.0f, rng.uniform(-p.trunkBend, p.trunkBend) };

        for (int i = 0; i <= p.trunkSegments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(p.trunkSegments);
            const float swayX = std::sin(t * 2.2f) * drift.x * 0.35f;
            const float swayZ = std::cos(t * 1.8f) * drift.z * 0.35f;
            curve.push_back({
                swayX + rng.uniform(-p.trunkNoise, p.trunkNoise) * t,
                t * p.trunkHeight,
                swayZ + rng.uniform(-p.trunkNoise, p.trunkNoise) * t
            });
        }

        return curve;
    }

    std::vector<float> makeTrunkRadii(const TreeParams& p) {
        std::vector<float> radii;
        radii.reserve(p.trunkSegments + 1);

        for (int i = 0; i <= p.trunkSegments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(p.trunkSegments);
            radii.push_back(lerp(p.trunkRadiusBase, p.trunkRadiusTop, std::pow(t, 0.8f)));
        }
        return radii;
    }

    std::vector<Vec3> makeBranchCurve(
        const Vec3& start,
        const Vec3& trunkDir,
        float lengthValue,
        float upBias,
        int segments,
        RNG& rng) {
        std::vector<Vec3> pts;
        pts.reserve(segments + 1);
        pts.push_back(start);

        Vec3 side = normalize(cross(trunkDir, Vec3{ 0.0f, 1.0f, 0.0f }));
        if (length(side) < 1e-5f) {
            side = { 1.0f, 0.0f, 0.0f };
        }

        const float angle = rng.uniform(0.0f, 2.0f * 3.1415926535f);
        const Vec3 radial = normalize(side * std::cos(angle) + cross(trunkDir, side) * std::sin(angle));
        Vec3 dir = normalize(radial * (1.0f - upBias) + Vec3{ 0.0f, 1.0f, 0.0f } * upBias);

        for (int i = 1; i <= segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            dir = normalize(dir + Vec3{
                rng.uniform(-0.25f, 0.25f) * t,
                rng.uniform(0.05f, 0.25f) * t,
                rng.uniform(-0.25f, 0.25f) * t
            });
            pts.push_back(pts.back() + dir * (lengthValue / static_cast<float>(segments)));
        }

        return pts;
    }

    std::vector<float> makeBranchRadii(float baseRadius, int segments) {
        std::vector<float> radii;
        radii.reserve(segments + 1);
        for (int i = 0; i <= segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            radii.push_back(lerp(baseRadius, baseRadius * 0.25f, t));
        }
        return radii;
    }

    TreeMeshes generateStylizedTree(const TreeParams& p) {
        RNG rng(p.seed);
        TreeMeshes out;

        // Генерация ствола
        const std::vector<Vec3> trunk = makeTrunkCurve(p, rng);
        const std::vector<float> trunkRadii = makeTrunkRadii(p);
        appendTube(out.wood, trunk, trunkRadii, p.radialSegments);

        // Генерация веток
        for (int i = 0; i < p.branchCount; ++i) {
            const float t = rng.uniform(0.28f, 0.88f);
            const float fIndex = t * static_cast<float>(p.trunkSegments);
            const int i0 = std::clamp(static_cast<int>(std::floor(fIndex)), 0, p.trunkSegments - 1);
            const int i1 = i0 + 1;
            const float localT = fIndex - static_cast<float>(i0);

            const Vec3 start = lerp(trunk[i0], trunk[i1], localT);
            const Vec3 trunkDir = normalize(trunk[i1] - trunk[i0]);
            const float baseTrunkRadius = lerp(trunkRadii[i0], trunkRadii[i1], localT);
            const float branchRadius = std::max(0.02f, baseTrunkRadius * p.branchRadiusFactor);
            const float branchLength = rng.uniform(p.branchLengthMin, p.branchLengthMax);

            const auto branch = makeBranchCurve(start, trunkDir, branchLength, p.branchUpBias, p.branchSegments, rng);
            const auto branchRadii = makeBranchRadii(branchRadius, p.branchSegments);
            appendTube(out.wood, branch, branchRadii, std::max(6, p.radialSegments - 2));

            // Сохраняем конец ветки
            const Vec3 tip = branch.back();
            out.branchTips.push_back(tip);
        }

        return out;
    }
    void appendToSimpleMesh(simple3d::Mesh& dst, const Mesh& src, uint32_t materialIndex = UINT32_MAX) {
        const uint32_t vertexOffset = static_cast<uint32_t>(dst.vertexCount());

        for (const Vertex& vertex : src.vertices) {
            dst.positions.insert(dst.positions.end(), { vertex.pos.x, vertex.pos.y, vertex.pos.z });
            dst.normals.insert(dst.normals.end(), { vertex.normal.x, vertex.normal.y, vertex.normal.z });
            dst.texcoords.insert(dst.texcoords.end(), { vertex.uv.x, vertex.uv.y });
        }

        for (uint32_t index : src.indices) {
            dst.indices.push_back(vertexOffset + index);
        }

        if (materialIndex != UINT32_MAX) {
            const size_t triCount = src.indices.size() / 3;
            dst.faceMaterial.insert(dst.faceMaterial.end(), triCount, materialIndex);
        }
    }

    void appendGeneratedStylizedTreeMeshes(ContributorAsset& asset) {
        struct TreeVariant {
            TreeParams params;
            float offsetX;
            QString name;
        };

        std::vector<TreeVariant> variants = {
            // 1. Дуб — прежний, раскидистый
            {
                {14, 10, 5.0f, 0.40f, 0.12f, 8, 5, 1.2f, 2.5f, 0.42f, 0.50f, 0.5f, 0.20f, 6, 8, 0.50f, 0.95f, 3, 42},
                -6.0f, "Oak"
            },
            // 2. Яблоня — невысокая, округлая крона
            {
                {14, 10, 3.5f, 0.35f, 0.12f, 7, 6, 1.0f, 1.8f, 0.40f, 0.55f, 0.3f, 0.12f, 6, 8, 0.55f, 0.85f, 3, 201},
                -2.0f, "Apple"
            },
            // 3. Берёза — стройная, ветки вверх
            {
                {14, 10, 6.5f, 0.25f, 0.06f, 10, 6, 1.0f, 1.8f, 0.28f, 0.70f, 0.15f, 0.08f, 6, 8, 0.40f, 0.70f, 2, 301},
                2.0f, "Birch"
            },
            // 4. Акация — прежняя, зонтичная
            {
                {14, 10, 7.0f, 0.25f, 0.15f, 5, 5, 2.0f, 3.5f, 0.28f, 0.85f, 0.2f, 0.10f, 6, 8, 0.40f, 0.75f, 2, 300},
                6.0f, "Acacia"
            },
        };

        for (const auto& variant : variants) {
            TreeParams p = variant.params;
            TreeMeshes tree = generateStylizedTree(p);

            for (auto& v : tree.wood.vertices) {
                v.pos.x += variant.offsetX;
            }

            Mesh anonymousWoodMesh;
            anonymousWoodMesh.vertices = tree.wood.vertices;
            anonymousWoodMesh.indices = tree.wood.indices;
            appendToSimpleMesh(asset.generatedWoodMesh, anonymousWoodMesh);

            for (const auto& tip : tree.branchTips) {
                asset.branchTips.push_back(QVector3D(tip.x + variant.offsetX, tip.y, tip.z));
            }
        }

        asset.generatedMesh = simple3d::Mesh{};
        asset.generatedMesh.materialNames = { "trunk" };

        Mesh combinedAnonymous;
        for (size_t i = 0; i < asset.generatedWoodMesh.vertexCount(); ++i) {
            Vertex v;
            v.pos.x = asset.generatedWoodMesh.positions[i * 3 + 0];
            v.pos.y = asset.generatedWoodMesh.positions[i * 3 + 1];
            v.pos.z = asset.generatedWoodMesh.positions[i * 3 + 2];
            if (!asset.generatedWoodMesh.normals.empty()) {
                v.normal.x = asset.generatedWoodMesh.normals[i * 3 + 0];
                v.normal.y = asset.generatedWoodMesh.normals[i * 3 + 1];
                v.normal.z = asset.generatedWoodMesh.normals[i * 3 + 2];
            }
            if (!asset.generatedWoodMesh.texcoords.empty()) {
                v.uv.x = asset.generatedWoodMesh.texcoords[i * 2 + 0];
                v.uv.y = asset.generatedWoodMesh.texcoords[i * 2 + 1];
            }
            combinedAnonymous.vertices.push_back(v);
        }
        combinedAnonymous.indices = asset.generatedWoodMesh.indices;
        appendToSimpleMesh(asset.generatedMesh, combinedAnonymous, 0u);

        printf("Total branch tips: %d\n", (int)asset.branchTips.size());
    }
} // namespace

ContributorAsset buildContributorAsset() {
    ContributorAsset asset;

    asset.source = ContributorAssetSource::GeneratedMesh;
    appendGeneratedStylizedTreeMeshes(asset);

    // ========== НАСТРОЙКИ РЕНДЕРИНГА ==========
    const float treeScale = 0.5f;

    asset.render.position = QVector3D(0.0f, 0.0f, 0.0f);
    asset.render.rotationDegrees = QVector3D(0.0f, 0.0f, 0.0f);
    asset.render.scale = treeScale;
    asset.render.fallbackColor = QVector3D(0.24f, 0.62f, 0.22f);
    asset.woodColor = QVector3D(0.46f, 0.27f, 0.12f);
    asset.leavesColor = QVector3D(0.18f, 0.58f, 0.20f);

    // ========== ГЕНЕРАЦИЯ ЧАСТИЦ ==========
    std::mt19937 rng(12345);

    // Распределения для размеров блобов
    std::uniform_real_distribution<float> distRadiusSmall(0.35f, 0.45f);
    std::uniform_real_distribution<float> distRadiusMedium(0.45f, 0.55f);
    std::uniform_real_distribution<float> distRadiusLarge(0.55f, 0.65f);
    std::uniform_real_distribution<float> distType(0.0f, 1.0f);

    std::uniform_real_distribution<float> distCountSmall(200, 400);
    std::uniform_real_distribution<float> distCountMedium(400, 800);
    std::uniform_real_distribution<float> distCountLarge(800, 1200);

    // Цвета
    std::uniform_real_distribution<float> distGreen(0.18f, 0.28f);
    std::uniform_real_distribution<float> distGreenG(0.60f, 0.75f);
    std::uniform_real_distribution<float> distGreenB(0.15f, 0.28f);

    std::vector<ContributorParticleBlob> blobs;

    // Для каждого конца ветки создаём облако частиц
    for (const auto& tip : asset.branchTips) {
        ContributorParticleBlob blob;
        blob.center = QVector3D(tip.x() * treeScale, tip.y() * treeScale, tip.z() * treeScale);

        float typeRand = distType(rng);

        if (typeRand < 0.33f) {
            blob.radius = distRadiusSmall(rng);
            blob.particleCount = int(distCountSmall(rng));
        }
        else if (typeRand < 0.66f) {
            blob.radius = distRadiusMedium(rng);
            blob.particleCount = int(distCountMedium(rng));
        }
        else {
            blob.radius = distRadiusLarge(rng);
            blob.particleCount = int(distCountLarge(rng));
        }

        blob.color = QVector3D(
            distGreen(rng),
            distGreenG(rng),
            distGreenB(rng)
        );

        blobs.push_back(blob);
    }

    // Собираем частицы
    asset.particles.clear();
    for (const auto& blob : blobs) {
        auto blobParticles = generateParticleBlob(blob, rng);
        asset.particles.insert(asset.particles.end(), blobParticles.begin(), blobParticles.end());
    }

    debugPrintParticleCount(asset.particles);
    qDebug() << "Number of branch tips:" << asset.branchTips.size();

    return asset;
}