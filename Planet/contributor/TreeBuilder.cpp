#include "contributor/TreeBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <QDebug>

namespace {

    // ===================== ¬—ѕќћќ√ј“≈Ћ№Ќјя ћј“≈ћј“» ј =====================

    struct Vec2 { float x = 0.0f; float y = 0.0f; };

    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        Vec3() = default;
        Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

        Vec3 operator+(const Vec3& r) const { return { x + r.x, y + r.y, z + r.z }; }
        Vec3 operator-(const Vec3& r) const { return { x - r.x, y - r.y, z - r.z }; }
        Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
        Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }

        Vec3& operator+=(const Vec3& r) { x += r.x; y += r.y; z += r.z; return *this; }
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
        return { a.y * b.z - a.z * b.y,
                 a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x };
    }

    float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

    Vec3 normalize(const Vec3& v) {
        const float len = length(v);
        if (len < 1e-8f) return { 0.0f, 1.0f, 0.0f };
        return v / len;
    }

    float lerp(float a, float b, float t) { return a + (b - a) * t; }
    Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a * (1.0f - t) + b * t; }

    Vec3 orthogonalUp(const Vec3& tangent) {
        const Vec3 up = (std::fabs(tangent.y) < 0.92f)
            ? Vec3{ 0.0f, 1.0f, 0.0f }
        : Vec3{ 1.0f, 0.0f, 0.0f };
        Vec3 right = normalize(cross(up, tangent));
        if (length(right) < 1e-6f) right = { 1.0f, 0.0f, 0.0f };
        return normalize(cross(tangent, right));
    }

    QVector3D toQ(const Vec3& v) { return QVector3D(v.x, v.y, v.z); }

    // ===================== √≈Ќ≈–ј÷»я ѕ–»ћ»“»¬ќ¬ =====================

    void appendTube(Mesh& mesh,
        const std::vector<Vec3>& points,
        const std::vector<float>& radii,
        int radialSegments) {
        if (points.size() < 2 || points.size() != radii.size()) return;

        const uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

        std::vector<Vec3> tangents(points.size());
        for (size_t i = 0; i < points.size(); ++i) {
            if (i == 0)             tangents[i] = normalize(points[1] - points[0]);
            else if (i + 1 == points.size()) tangents[i] = normalize(points[i] - points[i - 1]);
            else                    tangents[i] = normalize(points[i + 1] - points[i - 1]);
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
                mesh.vertices.push_back({
                    points[i] + radial * radii[i],
                    normalize(radial),
                    { u, v }
                    });
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

    // ===================== √≈Ќ≈–ј÷»я  –»¬џ’ =====================

    std::vector<Vec3> makeTrunkCurve(const TreeBuilder::TreeParams& p, RNG& rng) {
        std::vector<Vec3> curve;
        curve.reserve(p.trunkSegments + 1);

        const Vec3 drift{
            rng.uniform(-p.trunkBend, p.trunkBend),
            0.0f,
            rng.uniform(-p.trunkBend, p.trunkBend)
        };

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

    std::vector<float> makeTrunkRadii(const TreeBuilder::TreeParams& p) {
        std::vector<float> radii;
        radii.reserve(p.trunkSegments + 1);
        for (int i = 0; i <= p.trunkSegments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(p.trunkSegments);
            radii.push_back(lerp(p.trunkRadiusBase, p.trunkRadiusTop, std::pow(t, 0.8f)));
        }
        return radii;
    }

    std::vector<Vec3> makeBranchCurve(const Vec3& start,
        const Vec3& trunkDir,
        float lengthValue,
        float upBias,
        int segments,
        RNG& rng) {
        std::vector<Vec3> pts;
        pts.reserve(segments + 1);
        pts.push_back(start);

        Vec3 side = normalize(cross(trunkDir, Vec3{ 0.0f, 1.0f, 0.0f }));
        if (length(side) < 1e-5f) side = { 1.0f, 0.0f, 0.0f };

        const float angle = rng.uniform(0.0f, 2.0f * 3.1415926535f);
        const Vec3 radial = normalize(side * std::cos(angle) +
            cross(trunkDir, side) * std::sin(angle));
        Vec3 dir = normalize(radial * (1.0f - upBias) + Vec3{ 0.0f, 1.0f, 0.0f } *upBias);

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

    // =====================  ќЌ¬≈–“ј÷»я Mesh -> simple3d::Mesh =====================

    simple3d::Mesh toSimple3d(const Mesh& src) {
        simple3d::Mesh dst;
        dst.positions.reserve(src.vertices.size() * 3);
        dst.normals.reserve(src.vertices.size() * 3);
        dst.texcoords.reserve(src.vertices.size() * 2);

        for (const Vertex& v : src.vertices) {
            dst.positions.push_back(v.pos.x);
            dst.positions.push_back(v.pos.y);
            dst.positions.push_back(v.pos.z);
            dst.normals.push_back(v.normal.x);
            dst.normals.push_back(v.normal.y);
            dst.normals.push_back(v.normal.z);
            dst.texcoords.push_back(v.uv.x);
            dst.texcoords.push_back(v.uv.y);
        }
        dst.indices = src.indices;
        return dst;
    }

    // ===================== ќ—Ќќ¬Ќјя √≈Ќ≈–ј÷»я =====================

    TreeBuilder::TreeMeshData generateStylizedTree(const TreeBuilder::TreeParams& p) {
        RNG rng(p.seed);
        TreeBuilder::TreeMeshData out;
        Mesh woodMesh;

        // —твол
        const std::vector<Vec3> trunk = makeTrunkCurve(p, rng);
        const std::vector<float> trunkRadii = makeTrunkRadii(p);
        appendTube(woodMesh, trunk, trunkRadii, p.radialSegments);

        // ¬етки
        out.branchTips.reserve(p.branchCount);
        for (int i = 0; i < p.branchCount; ++i) {
            const float t = rng.uniform(0.28f, 0.88f);
            const float fIndex = t * static_cast<float>(p.trunkSegments);
            const int i0 = std::clamp(static_cast<int>(std::floor(fIndex)),
                0, p.trunkSegments - 1);
            const int i1 = i0 + 1;
            const float localT = fIndex - static_cast<float>(i0);

            const Vec3 start = lerp(trunk[i0], trunk[i1], localT);
            const Vec3 trunkDir = normalize(trunk[i1] - trunk[i0]);
            const float baseTrunkRadius = lerp(trunkRadii[i0], trunkRadii[i1], localT);
            const float branchRadius = std::max(0.02f, baseTrunkRadius * p.branchRadiusFactor);
            const float branchLength = rng.uniform(p.branchLengthMin, p.branchLengthMax);

            const auto branch = makeBranchCurve(start, trunkDir, branchLength,
                p.branchUpBias, p.branchSegments, rng);
            const auto branchRadii = makeBranchRadii(branchRadius, p.branchSegments);
            appendTube(woodMesh, branch, branchRadii,
                std::max(6, p.radialSegments - 2));

            out.branchTips.push_back(toQ(branch.back()));
        }

        out.wood = toSimple3d(woodMesh);
        return out;
    }

} // anonymous namespace

// ===================== ѕ”ЅЋ»„Ќќ≈ API =====================

TreeBuilder::TreeSpecies TreeBuilder::selectSpecies(Biome biome, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    const float r = dist(rng);

    switch (biome) {
    case Biome::Grass:
        if (r < 0.50f) return TreeSpecies::Oak;
        if (r < 0.80f) return TreeSpecies::Apple;
        return TreeSpecies::Birch;

    case Biome::Jungle:
        if (r < 0.40f) return TreeSpecies::Ceiba;
        if (r < 0.80f) return TreeSpecies::Tualang;
        return TreeSpecies::Kigelia;

    case Biome::Savanna:
        if (r < 0.70f) return TreeSpecies::Acacia;
        return TreeSpecies::Kigelia;

    default:
        return TreeSpecies::Oak;
    }
}

TreeBuilder::TreeParams TreeBuilder::baseParamsForSpecies(TreeSpecies species) {
    TreeParams p;
    switch (species) {
    case TreeSpecies::Oak:
        // было: { 14, 10, 5.0f, 0.40f, 0.12f, 8, 5, 1.2f, 2.5f, 0.42f, 0.50f, 0.5f, 0.20f, 6, 8, 0.50f, 0.95f, 6, 42u }
        p = { 10, 10, 2.0f, 0.15f, 0.05f, 10, 4, 0.6f, 1.2f, 0.25f, 0.60f, 0.3f, 0.10f, 6, 8, 0.50f, 0.95f, 6, 42u };
        break;
    case TreeSpecies::Apple:
        // было: { 14, 10, 3.5f, 0.35f, 0.12f, 7, 6, 1.0f, 1.8f, 0.40f, 0.55f, 0.3f, 0.12f, 6, 8, 0.55f, 0.85f, 5, 201u }
        p = { 10, 10, 1.4f, 0.12f, 0.04f, 10, 4, 0.5f, 1.0f, 0.25f, 0.55f, 0.2f, 0.08f, 6, 8, 0.55f, 0.85f, 5, 201u };
        break;
    case TreeSpecies::Birch:
        // было: { 14, 10, 6.5f, 0.25f, 0.06f, 10, 6, 1.0f, 1.8f, 0.28f, 0.70f, 0.15f, 0.08f, 6, 8, 0.40f, 0.70f, 5, 301u }
        p = { 12, 10, 2.6f, 0.10f, 0.03f, 12, 5, 0.6f, 1.2f, 0.20f, 0.70f, 0.15f, 0.06f, 6, 8, 0.40f, 0.70f, 5, 301u };
        break;
    case TreeSpecies::Acacia:
        // было: { 14, 10, 7.0f, 0.25f, 0.15f, 5, 5, 2.0f, 3.5f, 0.28f, 0.85f, 0.2f, 0.10f, 6, 8, 0.40f, 0.75f, 5, 300u }
        p = { 12, 10, 2.4f, 0.12f, 0.05f, 8, 4, 0.8f, 1.4f, 0.20f, 0.85f, 0.2f, 0.08f, 6, 8, 0.40f, 0.75f, 5, 300u };
        break;
    case TreeSpecies::Kigelia:
        // было: { 12, 10, 4.5f, 0.30f, 0.10f, 9, 5, 1.0f, 2.0f, 0.35f, 0.60f, 0.4f, 0.18f, 6, 8, 0.45f, 0.80f, 5, 401u }
        p = { 10, 10, 1.8f, 0.12f, 0.04f, 10, 4, 0.5f, 1.0f, 0.22f, 0.60f, 0.3f, 0.12f, 6, 8, 0.45f, 0.80f, 5, 401u };
        break;
    case TreeSpecies::Ceiba:
        // было: { 16, 12, 9.0f, 0.55f, 0.15f, 10, 6, 1.8f, 3.5f, 0.40f, 0.65f, 0.6f, 0.25f, 6, 8, 0.55f, 1.00f, 6, 501u }
        p = { 14, 12, 3.6f, 0.18f, 0.06f, 12, 5, 0.8f, 1.6f, 0.25f, 0.65f, 0.4f, 0.15f, 6, 8, 0.55f, 1.00f, 6, 501u };
        break;
    case TreeSpecies::Tualang:
        // было: { 18, 12, 11.0f, 0.60f, 0.18f, 12, 6, 2.0f, 4.0f, 0.42f, 0.60f, 0.7f, 0.28f, 6, 8, 0.60f, 1.10f, 6, 601u }
        p = { 16, 12, 4.4f, 0.20f, 0.07f, 14, 5, 0.9f, 1.8f, 0.28f, 0.60f, 0.5f, 0.18f, 6, 8, 0.60f, 1.10f, 6, 601u };
        break;
    }
    return p;
}
void TreeBuilder::colorsForSpecies(TreeSpecies species,
    QVector3D& foliageOut,
    QVector3D& trunkOut) {
    switch (species) {
    case TreeSpecies::Oak:
        foliageOut = QVector3D(0.15f, 0.45f, 0.15f);
        trunkOut = QVector3D(0.40f, 0.25f, 0.15f);
        break;
    case TreeSpecies::Apple:
        foliageOut = QVector3D(0.35f, 0.85f, 0.30f);
        trunkOut = QVector3D(0.50f, 0.35f, 0.20f);
        break;
    case TreeSpecies::Birch:
        foliageOut = QVector3D(0.20f, 0.65f, 0.50f);
        trunkOut = QVector3D(0.85f, 0.85f, 0.80f);
        break;
    case TreeSpecies::Acacia:
        foliageOut = QVector3D(0.80f, 0.55f, 0.20f);
        trunkOut = QVector3D(0.45f, 0.30f, 0.15f);
        break;
    case TreeSpecies::Kigelia:
        foliageOut = QVector3D(0.25f, 0.55f, 0.20f);
        trunkOut = QVector3D(0.50f, 0.40f, 0.30f);
        break;
    case TreeSpecies::Ceiba:
        foliageOut = QVector3D(0.30f, 0.75f, 0.25f);
        trunkOut = QVector3D(0.65f, 0.60f, 0.55f);
        break;
    case TreeSpecies::Tualang:
        foliageOut = QVector3D(0.20f, 0.60f, 0.20f);
        trunkOut = QVector3D(0.55f, 0.45f, 0.35f);
        break;
    }
}

float TreeBuilder::realisticScaleForSpecies(TreeSpecies species) {
    switch (species) {
    case TreeSpecies::Oak:     return 1.0f;
    case TreeSpecies::Apple:   return 0.9f;
    case TreeSpecies::Birch:   return 0.7f;
    case TreeSpecies::Acacia:  return 0.7f;
    case TreeSpecies::Kigelia: return 0.4f;
    case TreeSpecies::Ceiba:   return 1.4f;
    case TreeSpecies::Tualang: return 1.6f;
    }
    return 1.0f;
}

TreeBuilder::TreeVariant TreeBuilder::generateVariant(TreeSpecies species, uint32_t seed) {
    TreeVariant variant;
    variant.species = species;
    variant.seed = seed;
    variant.params = baseParamsForSpecies(species);
    variant.params.seed = seed;
    variant.realisticScale = realisticScaleForSpecies(species);

    std::mt19937 rng(seed);
    auto vary = [&rng](float base, float percent) {
        std::uniform_real_distribution<float> d(1.0f - percent, 1.0f + percent);
        return base * d(rng);
        };

    // ±20% высота
    variant.params.trunkHeight = vary(variant.params.trunkHeight, 0.20f);
    // ±2 ветки
    std::uniform_int_distribution<int> branchJitter(-2, 2);
    variant.params.branchCount = std::max(2, variant.params.branchCount + branchJitter(rng));
    // ±15% длина веток
    variant.params.branchLengthMin = vary(variant.params.branchLengthMin, 0.15f);
    variant.params.branchLengthMax = vary(variant.params.branchLengthMax, 0.15f);
    // ±40% изгиб
    variant.params.trunkBend = vary(variant.params.trunkBend, 0.40f);
    // ±20% радиус
    variant.params.trunkRadiusBase = vary(variant.params.trunkRadiusBase, 0.20f);
    variant.params.trunkRadiusTop = vary(variant.params.trunkRadiusTop, 0.20f);

    // ±5% цвета
    QVector3D baseFoliage, baseTrunk;
    colorsForSpecies(species, baseFoliage, baseTrunk);
    auto varyColor = [&rng](const QVector3D& c, float percent) {
        std::uniform_real_distribution<float> d(1.0f - percent, 1.0f + percent);
        return QVector3D(
            std::clamp(c.x() * d(rng), 0.0f, 1.0f),
            std::clamp(c.y() * d(rng), 0.0f, 1.0f),
            std::clamp(c.z() * d(rng), 0.0f, 1.0f));
        };
    variant.foliageColor = varyColor(baseFoliage, 0.20f);
    variant.trunkColor = varyColor(baseTrunk, 0.15f);

    return variant;
}

TreeBuilder::TreeMeshData TreeBuilder::generateMesh(const TreeParams& params) {
    return generateStylizedTree(params);
}

std::vector<ContributorParticle> TreeBuilder::generateParticles(
    const TreeMeshData& mesh,
    const TreeVariant& variant,
    float globalScale) {

    std::vector<ContributorParticle> result;
    if (mesh.branchTips.empty()) return result;

    std::mt19937 rng(variant.seed ^ 0xA5A5A5A5u);

    std::uniform_real_distribution<float> distRadiusSmall(0.35f, 0.45f);
    std::uniform_real_distribution<float> distRadiusMedium(0.45f, 0.55f);
    std::uniform_real_distribution<float> distRadiusLarge(0.55f, 0.65f);
    std::uniform_real_distribution<float> distType(0.0f, 1.0f);

    std::uniform_real_distribution<float> distCountSmall(3000.0f, 5000.0f);
    std::uniform_real_distribution<float> distCountMedium(6000.0f, 10000.0f);
    std::uniform_real_distribution<float> distCountLarge(12000.0f, 20000.0f);

    for (const auto& tip : mesh.branchTips) {
        ContributorParticleBlob blob;
        //  онцы веток уже в локальных координатах дерева.
        blob.center = tip;

        const float typeRand = distType(rng);
        if (typeRand < 0.33f) {
            blob.radius = distRadiusSmall(rng);
            blob.particleCount = static_cast<int>(distCountSmall(rng));
        }
        else if (typeRand < 0.66f) {
            blob.radius = distRadiusMedium(rng);
            blob.particleCount = static_cast<int>(distCountMedium(rng));
        }
        else {
            blob.radius = distRadiusLarge(rng);
            blob.particleCount = static_cast<int>(distCountLarge(rng));
        }

        // ÷вет варианта Ч уникальный дл€ каждого варианта
        blob.color = variant.foliageColor;

        auto blobParticles = generateParticleBlob(blob, rng);
        result.insert(result.end(), blobParticles.begin(), blobParticles.end());
    }

    return result;
}

std::vector<std::vector<TreeBuilder::TreeVariant>> TreeBuilder::generateCache(int variantsPerSpecies) {
    std::vector<std::vector<TreeVariant>> cache;
    cache.reserve(kTreeSpeciesCount);

    for (int s = 0; s < kTreeSpeciesCount; ++s) {
        TreeSpecies species = static_cast<TreeSpecies>(s);
        std::vector<TreeVariant> variants;
        variants.reserve(variantsPerSpecies);

        for (int v = 0; v < variantsPerSpecies; ++v) {
            const uint32_t seed =
                (static_cast<uint32_t>(s) * 1000u) +
                (static_cast<uint32_t>(v) * 7919u) +
                0xDEADBEEFu;
            variants.push_back(generateVariant(species, seed));
        }

        // ? ƒќЅј¬Ћ≈Ќќ: лог параметров каждого варианта
        for (int v = 0; v < variantsPerSpecies; ++v) {
            const auto& var = variants[v];
            qDebug() << "Species" << s
                << "variant" << v
                << "| trunkHeight:" << var.params.trunkHeight
                << "| branchCount:" << var.params.branchCount
                << "| branchLen:" << var.params.branchLengthMin << "-" << var.params.branchLengthMax
                << "| trunkBend:" << var.params.trunkBend
                << "| foliage:" << var.foliageColor
                << "| trunk:" << var.trunkColor;
        }

        cache.push_back(std::move(variants));
    }
    return cache;
}