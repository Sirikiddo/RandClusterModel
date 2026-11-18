#include "renderers/TerrainTessellator.h"
#include "generation/RockAppearance.h"

#include <QFuture>
#include <QElapsedTimer>
#include <QDebug>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace {
} // namespace

QVector3D TerrainTessellator::slerpish(const QVector3D& a, const QVector3D& b, float t) {
    QVector3D x = (1.0f - t) * a + t * b;
    if (!x.isNull()) {
        x.normalize();
    }
    return x;
}

QVector3D TerrainTessellator::liftUnit(const QVector3D& unit, float h) const {
    if (!surfaceModel_) {
        return QVector3D();
    }
    return surfaceModel_->positionOnSurface(unit, h);
}

TerrainTessellator::EdgeMode TerrainTessellator::classifyEdge(int hA, int hB, int k) {
    const int d = std::abs(hA - hB);
    if (d == 0) {
        return EdgeMode::Flat;
    }
    if (d <= k) {
        return EdgeMode::Slope;
    }
    return EdgeMode::Cliff;
}

int TerrainTessellator::findLocalIndex(const Cell& c, int dv) {
    for (int i = 0; i < static_cast<int>(c.poly.size()); ++i) {
        if (c.poly[static_cast<size_t>(i)] == dv) {
            return i;
        }
    }
    return -1;
}

bool TerrainTessellator::isSeaEdge(const Cell& c, int edgeIdx, const std::vector<Cell>& cells) const {
    if (!coastalBand || edgeIdx < 0 || edgeIdx >= static_cast<int>(c.neighbors.size())) {
        return false;
    }
    const int neighborId = c.neighbors[static_cast<size_t>(edgeIdx)];
    return neighborId >= 0 &&
        neighborId < static_cast<int>(cells.size()) &&
        coastalBand->isSea(neighborId);
}

bool TerrainTessellator::isBeachLikeCell(const Cell& c) const {
    if (!coastalBand) {
        return false;
    }
    return coastalBand->isBeach(c.id) || coastalBand->isFlatCoast(c.id) || coastalBand->isCoastStepUp(c.id);
}

float TerrainTessellator::beachBlendForCell(const Cell& c) const {
    if (!coastalBand) {
        return 0.0f;
    }
    if (coastalBand->isSea(c.id)) {
        return coastalBand->isShallowSea(c.id) ? 0.55f : 0.0f;
    }
    if (coastalBand->isFlatCoast(c.id)) {
        return 1.0f;
    }
    if (coastalBand->isCoastStepUp(c.id)) {
        return 0.82f;
    }
    if (coastalBand->isBeach(c.id)) {
        return 0.62f;
    }
    return 0.0f;
}

QVector3D TerrainTessellator::colorForCell(const Cell& c) const {
    const WaterParams resolved = waterParams ? resolvedWaterParams(*waterParams, surfaceModel_) : WaterParams{};
    QVector3D baseColor = HexSphereModel::biomeColor(c.biome, c.temperature);
    if (c.biome == Biome::Sea) {
        baseColor = QVector3D(0.06f, 0.11f, 0.14f);
    }

    const float blend = beachBlendForCell(c);
    if (blend <= 0.0f) {
        return baseColor;
    }

    const bool wet = coastalBand && (coastalBand->isFlatCoast(c.id) || coastalBand->isSea(c.id) || coastalBand->isCoastStepUp(c.id));
    const QVector3D sand = wet ? resolved.wetSandColor : resolved.drySandColor;
    const QVector3D mixed = sand * blend + baseColor * (1.0f - blend);
    return mixed;
}

QVector3D TerrainTessellator::cliffColorForEdge(const Cell& c) const {
    const WaterParams resolved = waterParams ? resolvedWaterParams(*waterParams, surfaceModel_) : WaterParams{};
    const QVector3D rock(0.55f, 0.38f, 0.25f);
    const QVector3D sand = resolved.wetSandColor * 0.7f + resolved.drySandColor * 0.3f;
    const float blend = isBeachLikeCell(c) ? 0.65f : 0.0f;
    return rock * (1.0f - blend) + sand * blend;
}

float TerrainTessellator::bladeHeightForEdge(const Cell& c, int edgeIdx, const std::vector<Cell>& cells) const {
    const int nId = c.neighbors[static_cast<size_t>(edgeIdx)];
    if (nId >= 0 && classifyEdge(c.height, cells[static_cast<size_t>(nId)].height, smoothMaxDelta) == EdgeMode::Slope) {
        return 0.5f * static_cast<float>(c.height) + 0.5f * static_cast<float>(cells[static_cast<size_t>(nId)].height);
    }
    return static_cast<float>(c.height);
}

float TerrainTessellator::cornerBlendTargetHeight(const Cell& c, int i, const std::vector<Cell>& cells) const {
    const int deg = static_cast<int>(c.poly.size());
    const int iPrev = (i + deg - 1) % deg;

    const int nL = c.neighbors[static_cast<size_t>(iPrev)];
    const int nR = c.neighbors[static_cast<size_t>(i)];
    const bool hasL = nL >= 0;
    const bool hasR = nR >= 0;
    const int hL = hasL ? cells[static_cast<size_t>(nL)].height : c.height;
    const int hR = hasR ? cells[static_cast<size_t>(nR)].height : c.height;

    const bool smL = hasL && (classifyEdge(c.height, hL, smoothMaxDelta) == EdgeMode::Slope);
    const bool smR = hasR && (classifyEdge(c.height, hR, smoothMaxDelta) == EdgeMode::Slope);

    if (smL && smR) {
        return 0.5f * (static_cast<float>(hL) + static_cast<float>(hR));
    }
    if (smL) {
        return static_cast<float>(hL);
    }
    if (smR) {
        return static_cast<float>(hR);
    }
    return static_cast<float>(c.height);
}

TerrainTessellator::OreNoiseGenerator::OreNoiseGenerator(uint32_t seed) {
    std::array<int, 256> perm;
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), std::default_random_engine(seed));

    for (int i = 0; i < 256; ++i) {
        perm_[i] = perm_[i + 256] = perm[static_cast<size_t>(i)];
    }
}

float TerrainTessellator::OreNoiseGenerator::fade(float t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

float TerrainTessellator::OreNoiseGenerator::lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float TerrainTessellator::OreNoiseGenerator::grad(int hash, float x, float y, float z) {
    const int h = hash & 15;
    const float u = h < 8 ? x : y;
    const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float TerrainTessellator::OreNoiseGenerator::noise(float x, float y, float z, float time) const {
    const int xi = static_cast<int>(std::floor(x)) & 255;
    const int yi = static_cast<int>(std::floor(y)) & 255;
    const int zi = static_cast<int>(std::floor(z + time)) & 255;

    const float xf = x - std::floor(x);
    const float yf = y - std::floor(y);
    const float zf = z - std::floor(z);

    const float u = fade(xf);
    const float v = fade(yf);
    const float w = fade(zf);

    const int a = perm_[xi] + yi;
    const int aa = perm_[a] + zi;
    const int ab = perm_[a + 1] + zi;
    const int b = perm_[xi + 1] + yi;
    const int ba = perm_[b] + zi;
    const int bb = perm_[b + 1] + zi;

    const float x1 = lerp(grad(perm_[aa], xf, yf, zf), grad(perm_[ba], xf - 1, yf, zf), u);
    const float x2 = lerp(grad(perm_[ab], xf, yf - 1, zf), grad(perm_[bb], xf - 1, yf - 1, zf), u);
    const float y1 = lerp(x1, x2, v);

    const float x3 = lerp(grad(perm_[aa + 1], xf, yf, zf - 1), grad(perm_[ba + 1], xf - 1, yf, zf - 1), u);
    const float x4 = lerp(grad(perm_[ab + 1], xf, yf - 1, zf - 1), grad(perm_[bb + 1], xf - 1, yf - 1, zf - 1), u);
    const float y2 = lerp(x3, x4, v);

    return (lerp(y1, y2, w) + 1.0f) * 0.5f;
}

void TerrainTessellator::updateAnimation(float deltaTime) {
    if (enableOreVisualization) {
        animationTime_ += deltaTime * oreAnimationSpeed;
    }
}

QVector3D TerrainTessellator::calculateCellColorWithOre(
    const Cell& cell,
    const QVector3D& baseColor,
    const QVector3D& position) const {
    if (!enableOreVisualization || cell.oreDensity < 0.01f) {
        return baseColor;
    }

    float grainSize = 0.02f + cell.oreDensity * 0.08f;
    float grainContrast = 1.0f + cell.oreDensity * 2.0f;

    QVector3D oreColor;
    switch (cell.oreType) {
    case OreType::Iron:
        oreColor = QVector3D(0.7f, 0.4f, 0.2f);
        grainSize *= 1.2f;
        break;
    case OreType::Copper:
        oreColor = QVector3D(0.8f, 0.5f, 0.2f);
        grainContrast *= 1.5f;
        break;
    case OreType::Gold:
        oreColor = QVector3D(0.9f, 0.9f, 0.1f);
        grainSize *= 0.8f;
        grainContrast *= 2.0f;
        break;
    default:
        oreColor = QVector3D(0.5f, 0.5f, 0.5f);
        break;
    }

    const float noiseScale = 10.0f + cell.oreDensity * 50.0f;
    const float noise1 = oreNoise_.noise(position.x() * noiseScale, position.y() * noiseScale, position.z() * noiseScale, animationTime_);
    const float noise2 = oreNoise_.noise(position.x() * noiseScale * 2.3f, position.y() * noiseScale * 2.3f, position.z() * noiseScale * 2.3f, animationTime_ * 0.7f);
    const float combinedNoise = noise1 * 0.7f + noise2 * 0.3f;
    const float grainThreshold = 0.5f + cell.oreDensity * 0.3f;
    const float grainValue = std::sin(combinedNoise * 3.14159f * grainSize * 100.0f);

    if (grainValue > grainThreshold) {
        float grainIntensity = (grainValue - grainThreshold) / (1.0f - grainThreshold);
        grainIntensity = std::pow(grainIntensity, grainContrast);
        const QVector3D grainColor = oreColor * (0.8f + (cell.oreDensity * 0.4f));
        const float oreInfluence = grainIntensity * cell.oreDensity;
        return baseColor * (1.0f - oreInfluence) + grainColor * oreInfluence;
    }

    const float influence = std::max(0.0f, grainValue - 0.3f) / 0.2f;
    if (influence > 0.0f) {
        const float oreInfluence = influence * 0.2f * cell.oreDensity;
        return baseColor * (1.0f - oreInfluence) + oreColor * oreInfluence;
    }

    return baseColor;
}

TerrainTessellator::PreCell TerrainTessellator::makePreCell(const Cell& c, const std::vector<QVector3D>& dual) const {
    PreCell pc;
    const int deg = static_cast<int>(c.poly.size());
    pc.degree = deg;
    pc.h = static_cast<float>(c.height);
    pc.beachLike = isBeachLikeCell(c);
    pc.color = colorForCell(c);
    pc.center = liftUnit(c.centroid, pc.h);

    for (int i = 0; i < deg; ++i) {
        const QVector3D u = slerpish(dual[static_cast<size_t>(c.poly[static_cast<size_t>(i)])], c.centroid, inset);
        pc.inner[static_cast<size_t>(i)] = liftUnit(u, pc.h);
        pc.outerUnit[static_cast<size_t>(i)] = dual[static_cast<size_t>(c.poly[static_cast<size_t>(i)])];
    }
    const bool rocky = c.biome == Biome::Rock || c.biome == Biome::Tundra;
    if (rocky && !pc.beachLike && rockShapeAmount > 0.0f && deg >= 3) {
        float radius = 0.0f;
        for (int i = 0; i < deg; ++i) {
            radius += (pc.inner[static_cast<size_t>(i)] - pc.center).length();
        }
        radius /= static_cast<float>(deg);
        const float heightStep = surfaceModel_ ? surfaceModel_->heightStep() : 0.05f;
        const float biomeAmount = c.biome == Biome::Rock ? 1.0f : 0.35f;
        const float amplitude = std::min(heightStep * 0.20f, radius * 0.06f)
            * std::clamp(rockShapeAmount, 0.0f, 1.0f) * biomeAmount;
        const auto cellId = static_cast<uint32_t>(c.id);
        pc.center += pc.center.normalized() * amplitude
            * (0.25f + 0.75f * RockAppearance::random(cellId, rockSeed));
        for (int i = 0; i < deg; ++i) {
            auto& vertex = pc.inner[static_cast<size_t>(i)];
            const float chip = RockAppearance::random(cellId, rockSeed, static_cast<uint32_t>(i + 1));
            vertex += vertex.normalized() * amplitude * (chip - 0.5f) * 0.7f;
        }
        // Only the inset cap changes; shared outer edges retain their exact positions.
        pc.rockRelief = true;
    }
    return pc;
}

TerrainTessellator::TrimDirs TerrainTessellator::makeTrimDirs(const PreCell& pc) const {
    TrimDirs td;
    const int deg = pc.degree;
    const float t = std::clamp(outerTrim, 0.0f, 0.49f);

    auto U = [&](int k) -> QVector3D { return pc.outerUnit[static_cast<size_t>((k % deg + deg) % deg)]; };

    for (int i = 0; i < deg; ++i) {
        const int j = (i + 1) % deg;
        const int iPrev = (i + deg - 1) % deg;
        td.sideL[static_cast<size_t>(i)] = slerpish(U(i), U(j), t);
        td.sideR[static_cast<size_t>(i)] = slerpish(U(i), U(j), 1.0f - t);
        td.prevU[static_cast<size_t>(i)] = slerpish(U(iPrev), U(i), 1.0f - t);
        td.currU[static_cast<size_t>(i)] = slerpish(U(i), U(j), t);
        td.apexU[static_cast<size_t>(i)] = U(i);
    }
    return td;
}

TerrainTessellator::EdgeHeights TerrainTessellator::makeHeights(
    const Cell& c,
    const PreCell& pc,
    const std::vector<Cell>& cells) const {
    const int deg = static_cast<int>(c.poly.size());
    EdgeHeights eh;
    for (int i = 0; i < deg; ++i) {
        eh.edgeH[static_cast<size_t>(i)] = bladeHeightForEdge(c, i, cells);
        const float hBlend = cornerBlendTargetHeight(c, i, cells);
        eh.apexH[static_cast<size_t>(i)] = 0.5f * (pc.h + hBlend);
    }
    return eh;
}

void TerrainTessellator::MeshBuilder::setDirectRange(size_t begin, size_t end) {
    direct = true;
    triangleCursor = begin;
    triangleLimit = end;
    directPos = pos.data(); directCol = col.data(); directNorm = norm.data();
    directOre = ore.data(); directSurface = surface.data(); directIdx = idx.data();
    directOwner = owner ? owner->data() : nullptr;
    directRole = surfaceRole ? surfaceRole->data() : nullptr;
}

void TerrainTessellator::MeshBuilder::triToward(
    QVector3D A,
    QVector3D B,
    QVector3D C,
    const QVector3D& color,
    const QVector3D& toward,
    int cellOwner,
    TriangleSurfaceRole role,
    NormalMode normalMode) {
    QVector3D n = QVector3D::crossProduct(B - A, C - A).normalized();
    QVector3D t = toward;
    if (!t.isNull()) {
        t.normalize();
    }
    if (QVector3D::dotProduct(n, t) < 0.0f) {
        std::swap(B, C);
        n = -n;
    }

    const uint32_t base = direct ? static_cast<uint32_t>(triangleCursor * 3u)
        : vertexBase + static_cast<uint32_t>(pos.size() / 3);
    QVector3D faceNormal = n;
    if (normalMode == NormalMode::Face && faceNormal.isNull()) {
        // Qt rounds very small cross products to zero at high subdivision.
        // Normalize explicitly for lighting, keeping the existing triangle winding.
        faceNormal = QVector3D::crossProduct(B - A, C - A);
        const float length = faceNormal.length();
        faceNormal = length > 0.0f ? faceNormal / length : t;
        if (QVector3D::dotProduct(faceNormal, t) < 0.0f) faceNormal = -faceNormal;
    }
    // Keep face winding above; only the lighting normals change on level surfaces.
    const QVector3D nA = normalMode == NormalMode::Radial ? A.normalized() : faceNormal;
    const QVector3D nB = normalMode == NormalMode::Radial ? B.normalized() : faceNormal;
    const QVector3D nC = normalMode == NormalMode::Radial ? C.normalized() : faceNormal;

    float oreDensity = 0.0f;
    float oreType = 0.0f;

    // UV и вес камня: нужны для текстурирования террейна.
    float rockWeight = 0.0f;
    QVector3D uvOrigin(0.0f, 0.0f, 0.0f);
    QVector3D uvAxisU(1.0f, 0.0f, 0.0f);
    QVector3D uvAxisV(0.0f, 0.0f, 1.0f);

    if (cells && cellOwner >= 0 && cellOwner < static_cast<int>(cells->size())) {
        const Cell& cell = (*cells)[static_cast<size_t>(cellOwner)];
        if (cell.biome == Biome::Rock && cell.oreType != OreType::None && cell.oreDensity > 0.0f) {
            oreDensity = std::clamp(cell.oreDensity, 0.0f, 1.0f);
            oreType = static_cast<float>(cell.oreType);
        }

        rockWeight = (cell.biome == Biome::Rock || cell.biome == Biome::Tundra) ? 1.0f : 0.0f;

        uvOrigin = cell.centroid;
        const QVector3D up = cell.centroid.normalized();
        const QVector3D seed = (std::fabs(up.z()) > 0.99f) ? QVector3D(1.0f, 0.0f, 0.0f) : QVector3D(0.0f, 0.0f, 1.0f);
        const QVector3D tangent = QVector3D::crossProduct(seed, up).normalized();
        const QVector3D bitangent = QVector3D::crossProduct(up, tangent).normalized();
        const uint32_t hashCell = uint32_t(cellOwner) * 2654435761u;
        const float angle = float(hashCell & 1023u) * (6.2831853f / 1024.0f);
        const float ca = std::cos(angle);
        const float sa = std::sin(angle);
        uvAxisU = (tangent * ca + bitangent * sa).normalized();
        uvAxisV = (bitangent * ca - tangent * sa).normalized();
    }
    if (direct) {
        if (triangleCursor >= triangleLimit) throw std::logic_error("Terrain writer exceeded its assigned range");
        const float positions[]{A.x(), A.y(), A.z(), B.x(), B.y(), B.z(), C.x(), C.y(), C.z()};
        const float colors[]{color.x(), color.y(), color.z(), color.x(), color.y(), color.z(), color.x(), color.y(), color.z()};
        const float normals[]{nA.x(), nA.y(), nA.z(), nB.x(), nB.y(), nB.z(), nC.x(), nC.y(), nC.z()};
        const float ores[]{oreDensity, oreType, oreDensity, oreType, oreDensity, oreType};
        std::copy_n(positions, 9, directPos + triangleCursor * 9u);
        std::copy_n(colors, 9, directCol + triangleCursor * 9u);
        std::copy_n(normals, 9, directNorm + triangleCursor * 9u);
        std::copy_n(ores, 6, directOre + triangleCursor * 6u);
        const auto writeSurfaceDirect = [&](float* dst, const QVector3D& p) {
            const QVector3D d = p - uvOrigin;
            dst[0] = QVector3D::dotProduct(d, uvAxisU);
            dst[1] = QVector3D::dotProduct(d, uvAxisV);
            dst[2] = rockWeight;
            dst[3] = static_cast<float>(std::max(cellOwner, 0));
        };
        writeSurfaceDirect(directSurface + triangleCursor * 12u, A);
        writeSurfaceDirect(directSurface + triangleCursor * 12u + 4u, B);
        writeSurfaceDirect(directSurface + triangleCursor * 12u + 8u, C);
        directIdx[triangleCursor * 3u] = base;
        directIdx[triangleCursor * 3u + 1] = base + 1;
        directIdx[triangleCursor * 3u + 2] = base + 2;
        if (directOwner) directOwner[triangleCursor] = cellOwner;
        if (directRole) directRole[triangleCursor] = role;
        ++triangleCursor;
        return;
    }
    pos.insert(pos.end(), { A.x(), A.y(), A.z(), B.x(), B.y(), B.z(), C.x(), C.y(), C.z() });
    col.insert(col.end(), { color.x(), color.y(), color.z(), color.x(), color.y(), color.z(), color.x(), color.y(), color.z() });
    norm.insert(norm.end(), { nA.x(), nA.y(), nA.z(), nB.x(), nB.y(), nB.z(), nC.x(), nC.y(), nC.z() });
    ore.insert(ore.end(), {
        oreDensity, oreType,
        oreDensity, oreType,
        oreDensity, oreType
        });
    idx.insert(idx.end(), { base, base + 1, base + 2 });

    const auto pushSurface = [&](const QVector3D& p) {
        const QVector3D d = p - uvOrigin;
        surface.push_back(QVector3D::dotProduct(d, uvAxisU));
        surface.push_back(QVector3D::dotProduct(d, uvAxisV));
        surface.push_back(rockWeight);
        surface.push_back(static_cast<float>(std::max(cellOwner, 0)));
        };
    pushSurface(A);
    pushSurface(B);
    pushSurface(C);

    if (owner) {
        owner->push_back(cellOwner);
    }
    if (surfaceRole) {
        surfaceRole->push_back(role);
    }
}

void TerrainTessellator::MeshBuilder::quadToward(
    const QVector3D& Q0,
    const QVector3D& Q1,
    const QVector3D& Q2,
    const QVector3D& Q3,
    const QVector3D& color,
    const QVector3D& toward,
    int cellOwner,
    TriangleSurfaceRole role,
    NormalMode normalMode) {
    triToward(Q0, Q1, Q2, color, toward, cellOwner, role, normalMode);
    triToward(Q0, Q2, Q3, color, toward, cellOwner, role, normalMode);
}

void TerrainTessellator::buildInnerFan(MeshBuilder& mb, const Cell& c, const PreCell& pc) const {
    const int deg = static_cast<int>(c.poly.size());
    for (int i = 0; i < deg; ++i) {
        const QVector3D& A = pc.inner[static_cast<size_t>(i)];
        const QVector3D& B = pc.inner[static_cast<size_t>((i + 1) % deg)];
        mb.triToward(pc.center, A, B, pc.color, pc.center, c.id,
            TriangleSurfaceRole::Top, pc.rockRelief ? NormalMode::Face : NormalMode::Radial);
    }
}

void TerrainTessellator::buildBlades(
    MeshBuilder& mb,
    const Cell& c,
    const PreCell& pc,
    const TrimDirs& td,
    const EdgeHeights& eh) const {
    const int deg = static_cast<int>(c.poly.size());
    for (int i = 0; i < deg; ++i) {
        const int j = (i + 1) % deg;
        const QVector3D O0 = liftUnit(td.sideL[static_cast<size_t>(i)], eh.edgeH[static_cast<size_t>(i)]);
        const QVector3D O1 = liftUnit(td.sideR[static_cast<size_t>(i)], eh.edgeH[static_cast<size_t>(i)]);
        const QVector3D toward = pc.inner[static_cast<size_t>(i)] + pc.inner[static_cast<size_t>(j)] + O0 + O1;
        const NormalMode normalMode = !pc.rockRelief && pc.h == eh.edgeH[static_cast<size_t>(i)]
            ? NormalMode::Radial : NormalMode::Face;
        mb.quadToward(pc.inner[static_cast<size_t>(i)], pc.inner[static_cast<size_t>(j)], O1, O0, pc.color, toward, c.id,
            TriangleSurfaceRole::Top, normalMode);
        if (mb.beachTriCount && coastalBand && isBeachLikeCell(c) && ((coastalBand->shoreEdgeMask[static_cast<size_t>(c.id)] >> i) & 1u) != 0u) {
            *mb.beachTriCount += 2;
        }
    }
}

void TerrainTessellator::buildCorners(
    MeshBuilder& mb,
    const Cell& c,
    const PreCell& pc,
    const TrimDirs& td,
    const EdgeHeights& eh) const {
    const int deg = static_cast<int>(c.poly.size());
    for (int i = 0; i < deg; ++i) {
        const int iPrev = (i + deg - 1) % deg;
        const QVector3D I = pc.inner[static_cast<size_t>(i)];
        const QVector3D Ocurr = liftUnit(td.sideL[static_cast<size_t>(i)], eh.edgeH[static_cast<size_t>(i)]);
        const QVector3D Oprev = liftUnit(td.sideR[static_cast<size_t>(iPrev)], eh.edgeH[static_cast<size_t>(iPrev)]);
        const QVector3D Apex = liftUnit(td.apexU[static_cast<size_t>(i)], eh.apexH[static_cast<size_t>(i)]);

        // Test each corner triangle independently: one side can be level while the other slopes.
        const bool levelApex = pc.h == eh.apexH[static_cast<size_t>(i)];
        const NormalMode currMode = !pc.rockRelief && levelApex && pc.h == eh.edgeH[static_cast<size_t>(i)]
            ? NormalMode::Radial : NormalMode::Face;
        const NormalMode prevMode = !pc.rockRelief && levelApex && pc.h == eh.edgeH[static_cast<size_t>(iPrev)]
            ? NormalMode::Radial : NormalMode::Face;
        mb.triToward(I, Ocurr, Apex, pc.color, I + Ocurr + Apex, c.id,
            TriangleSurfaceRole::Top, currMode);
        mb.triToward(I, Oprev, Apex, pc.color, I + Oprev + Apex, c.id,
            TriangleSurfaceRole::Top, prevMode);
        if (mb.beachTriCount && coastalBand && isBeachLikeCell(c) && ((coastalBand->shoreVertexMask[static_cast<size_t>(c.id)] >> i) & 1u) != 0u) {
            *mb.beachTriCount += 2;
        }
    }
}

void TerrainTessellator::buildEdgeSide(
    size_t cid,
    const Cell& c,
    int iEdge,
    const PreCell& pc,
    const TrimDirs& td,
    const EdgeHeights& eh,
    EdgeInsertion& out) const {
    const int deg = static_cast<int>(c.poly.size());
    const int j = (iEdge + 1) % deg;
    const int iPrev = (iEdge + deg - 1) % deg;

    const int dv_i = c.poly[static_cast<size_t>(iEdge)];
    const int dv_j = c.poly[static_cast<size_t>(j)];
    out.key = EdgeKey{ std::min(dv_i, dv_j), std::max(dv_i, dv_j) };
    const bool canon = (dv_i <= dv_j);

    EdgeSide& S = out.side;
    S.cellId = static_cast<int>(cid);
    S.hCell = c.height;
    S.Hedge = eh.edgeH[static_cast<size_t>(iEdge)];
    S.Hleft = eh.edgeH[static_cast<size_t>(iPrev)];
    S.Hright = eh.edgeH[static_cast<size_t>(iEdge)];
    S.apexL = eh.apexH[static_cast<size_t>(iEdge)];
    S.apexR = eh.apexH[static_cast<size_t>(j)];
    S.sideL = td.sideL[static_cast<size_t>(iEdge)];
    S.sideR = td.sideR[static_cast<size_t>(iEdge)];
    S.prevU_L = td.prevU[static_cast<size_t>(iEdge)];
    S.currU_R = td.currU[static_cast<size_t>(iEdge)];
    S.apexDirL = td.apexU[static_cast<size_t>(iEdge)];
    S.apexDirR = td.apexU[static_cast<size_t>(j)];
    S.centroid = c.centroid;
    S.beachLike = pc.beachLike;

    if (!canon) {
        std::swap(S.apexL, S.apexR);
        std::swap(S.prevU_L, S.currU_R);
        std::swap(S.apexDirL, S.apexDirR);
        std::swap(S.sideL, S.sideR);
        std::swap(S.Hleft, S.Hright);
    }

    S.P_edgeL = liftUnit(S.sideL, S.Hedge);
    S.P_edgeR = liftUnit(S.sideR, S.Hedge);
    S.P_apexL = liftUnit(S.apexDirL, S.apexL);
    S.P_apexR = liftUnit(S.apexDirR, S.apexR);
}

void TerrainTessellator::insertEdgeSide(EdgeRegistry& reg, EdgeInsertion&& insertion) {
    auto& rec = reg[insertion.key];
    if (!rec.A) {
        rec.A = std::move(insertion.side);
    }
    else {
        rec.B = std::move(insertion.side);
    }
}

void TerrainTessellator::registerEdgeSide(
    EdgeRegistry& reg,
    size_t cid,
    const Cell& c,
    int iEdge,
    const PreCell& pc,
    const TrimDirs& td,
    const EdgeHeights& eh) const {
    EdgeInsertion insertion;
    buildEdgeSide(cid, c, iEdge, pc, td, eh, insertion);
    insertEdgeSide(reg, std::move(insertion));
}

void TerrainTessellator::finalizeCliffs(
    const EdgeRegistry& reg,
    MeshBuilder& mb,
    const std::vector<Cell>& cells) const {
    auto towardDir = [&](const EdgeSide& hi, const EdgeSide& lo) {
        QVector3D t = cells[static_cast<size_t>(lo.cellId)].centroid - cells[static_cast<size_t>(hi.cellId)].centroid;
        if (t.isNull()) {
            t = (hi.P_edgeL + hi.P_edgeR + lo.P_edgeL + lo.P_edgeR);
        }
        return t;
        };

    auto diff = [&](const QVector3D& a, const QVector3D& b) {
        return (a - b).lengthSquared() > (epsApex * epsApex);
        };

    for (const auto& [key, rec] : reg) {
        (void)key;
        if (!rec.A || !rec.B) {
            continue;
        }
        const EdgeSide& A = *rec.A;
        const EdgeSide& B = *rec.B;

        const EdgeMode mode = classifyEdge(A.hCell, B.hCell, smoothMaxDelta);
        const bool AisHigh = A.hCell > B.hCell;
        const EdgeSide& hi = AisHigh ? A : B;
        const EdgeSide& lo = AisHigh ? B : A;
        const QVector3D toward = towardDir(hi, lo);
        const QVector3D cliffColor = (hi.beachLike || lo.beachLike)
            ? cliffColorForEdge(cells[static_cast<size_t>(hi.cellId)])
            : QVector3D(0.55f, 0.38f, 0.25f);

        if (mode == EdgeMode::Cliff) {
            mb.quadToward(hi.P_edgeL, hi.P_edgeR, lo.P_edgeR, lo.P_edgeL, cliffColor, toward, hi.cellId, TriangleSurfaceRole::Cliff);
            mb.quadToward(hi.P_edgeL, hi.P_apexL, lo.P_apexL, lo.P_edgeL, cliffColor, toward, hi.cellId, TriangleSurfaceRole::Cliff);
            mb.quadToward(hi.P_edgeR, hi.P_apexR, lo.P_apexR, lo.P_edgeR, cliffColor, toward, hi.cellId, TriangleSurfaceRole::Cliff);
        }
        else if (mode == EdgeMode::Slope) {
            if (diff(A.P_apexL, B.P_apexL)) {
                const bool aHigher = A.apexL > B.apexL;
                mb.triToward(
                    aHigher ? A.P_apexL : B.P_apexL,
                    aHigher ? A.P_edgeL : B.P_edgeL,
                    aHigher ? B.P_apexL : A.P_apexL,
                    cliffColor,
                    toward,
                    aHigher ? A.cellId : B.cellId,
                    TriangleSurfaceRole::Slope);
            }
            if (diff(A.P_apexR, B.P_apexR)) {
                const bool aHigher = A.apexR > B.apexR;
                mb.triToward(
                    aHigher ? A.P_apexR : B.P_apexR,
                    aHigher ? A.P_edgeR : B.P_edgeR,
                    aHigher ? B.P_apexR : A.P_apexR,
                    cliffColor,
                    toward,
                    aHigher ? A.cellId : B.cellId,
                    TriangleSurfaceRole::Slope);
            }
        }
    }
}

TerrainMesh TerrainTessellator::build(const HexSphereModel& model) const {
    surfaceModel_ = &model;
    TerrainMesh M;
    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();
    const std::size_t cellCount = cells.size();
    QElapsedTimer phaseTimer;
    phaseTimer.start();

    // Count main geometry and non-flat edges first: exact reserves, chunk
    // vertex offsets and the final index space must be known before workers
    // write into the shared output.
    std::vector<std::uint32_t> cellTriangles(cellCount, 0u);
    std::size_t mainTriangles = 0;
    std::size_t cliffTriangleBound = 0;
    std::size_t nonFlatEdges = 0;
    for (size_t cid = 0; cid < cellCount; ++cid) {
        const Cell& c = cells[cid];
        const int deg = static_cast<int>(c.poly.size());
        if (deg < 3 || deg > kMaxCellDegree) {
            continue;
        }
        std::uint32_t localTriangles = 0;
        if (doCaps) {
            localTriangles += static_cast<std::uint32_t>(deg);
        }
        if (doBlades) {
            localTriangles += static_cast<std::uint32_t>(2 * deg);
        }
        if (doCornerTris) {
            localTriangles += static_cast<std::uint32_t>(2 * deg);
        }
        cellTriangles[cid] = localTriangles;
        mainTriangles += localTriangles;
        if (!doEdgeCliffs) {
            continue;
        }
        for (int i = 0; i < deg; ++i) {
            const int nId = i < static_cast<int>(c.neighbors.size())
                ? c.neighbors[static_cast<std::size_t>(i)]
                : -1;
            if (nId < 0 || nId >= static_cast<int>(cellCount)
                || cells[static_cast<std::size_t>(nId)].id <= c.id) {
                continue;
            }
            const EdgeMode mode = classifyEdge(
                c.height, cells[static_cast<std::size_t>(nId)].height, smoothMaxDelta);
            if (mode == EdgeMode::Flat) {
                continue;
            }
            ++nonFlatEdges;
            cliffTriangleBound += mode == EdgeMode::Cliff ? 6u : 2u;
        }
    }
    const std::size_t totalTriangles = mainTriangles + cliffTriangleBound;
    const double countMs = phaseTimer.nsecsElapsed() / 1000000.0;
    phaseTimer.restart();
    auto resizeMesh = [](TerrainMesh& mesh, size_t triangles) {
        mesh.pos.resize(triangles * 9u); mesh.col.resize(triangles * 9u);
        mesh.norm.resize(triangles * 9u); mesh.ore.resize(triangles * 6u);
        mesh.surface.resize(triangles * 12u);
        mesh.idx.resize(triangles * 3u); mesh.triOwner.resize(triangles);
        mesh.triSurfaceRole.resize(triangles);
    };
    if (directWrite) resizeMesh(M, totalTriangles);
    const double allocateMs = phaseTimer.nsecsElapsed() / 1000000.0;
    phaseTimer.restart();
    double buildMs = 0.0;
    double mergeMs = 0.0;

    int workerCount = 1;
    if (parallel && cellCount >= kParallelCellThreshold) {
        workerCount = std::min(
            QThreadPool::globalInstance()->maxThreadCount(),
            static_cast<int>(cellCount / kParallelCellThreshold) + 1);
        workerCount = std::max(workerCount, 1);
    }

    struct CellChunk {
        TerrainMesh mesh;
        std::vector<EdgeInsertion> edges;
    };

    auto buildRange = [&](size_t begin, size_t end, std::uint32_t vertexBase) {
        CellChunk chunk;
        std::size_t chunkTriangles = 0;
        for (size_t cid = begin; cid < end; ++cid) {
            chunkTriangles += cellTriangles[cid];
        }
        if (!directWrite) {
            chunk.mesh.pos.reserve(chunkTriangles * 9u);
            chunk.mesh.col.reserve(chunkTriangles * 9u);
            chunk.mesh.norm.reserve(chunkTriangles * 9u);
            chunk.mesh.ore.reserve(chunkTriangles * 6u);
            chunk.mesh.surface.reserve(chunkTriangles * 12u);
            chunk.mesh.idx.reserve(chunkTriangles * 3u);
            chunk.mesh.triOwner.reserve(chunkTriangles);
            chunk.mesh.triSurfaceRole.reserve(chunkTriangles);
        }

        TerrainMesh& target = directWrite ? M : chunk.mesh;
        MeshBuilder mb{ target.pos, target.col, target.norm, target.ore, target.surface, target.idx };
        mb.owner = &target.triOwner;
        mb.surfaceRole = &target.triSurfaceRole;
        mb.beachTriCount = &chunk.mesh.beachTriCount;
        mb.cells = &cells;
        mb.vertexBase = vertexBase;
        if (directWrite) mb.setDirectRange(vertexBase / 3u, vertexBase / 3u + chunkTriangles);

        for (size_t cid = begin; cid < end; ++cid) {
            const Cell& c = cells[cid];
            const int deg = static_cast<int>(c.poly.size());
            if (deg < 3 || deg > kMaxCellDegree) {
                continue;
            }

            const PreCell pc = makePreCell(c, dual);
            const TrimDirs td = makeTrimDirs(pc);
            const EdgeHeights eh = makeHeights(c, pc, cells);

            if (doCaps) {
                buildInnerFan(mb, c, pc);
            }
            if (doBlades) {
                buildBlades(mb, c, pc, td, eh);
            }
            if (doCornerTris) {
                buildCorners(mb, c, pc, td, eh);
            }
            if (doEdgeCliffs) {
                for (int i = 0; i < deg; ++i) {
                    const int nId = i < static_cast<int>(c.neighbors.size())
                        ? c.neighbors[static_cast<std::size_t>(i)]
                        : -1;
                    if (nId < 0 || nId >= static_cast<int>(cellCount)) {
                        continue;
                    }
                    // Flat edges produce no cliff/slope geometry; registering
                    // them used to fill the registry with ~1M unused entries.
                    if (classifyEdge(c.height, cells[static_cast<std::size_t>(nId)].height,
                            smoothMaxDelta) == EdgeMode::Flat) {
                        continue;
                    }
                    EdgeInsertion insertion;
                    buildEdgeSide(cid, c, i, pc, td, eh, insertion);
                    chunk.edges.push_back(std::move(insertion));
                }
            }
        }
        if (directWrite && mb.triangleCursor != mb.triangleLimit)
            throw std::logic_error("Terrain writer did not fill its assigned range");
        return chunk;
    };

    EdgeRegistry reg;
    reg.reserve(nonFlatEdges);

    std::vector<CellChunk> chunks(static_cast<std::size_t>(workerCount));
    std::vector<std::uint32_t> chunkVertexBase(static_cast<std::size_t>(workerCount), 0u);
    std::vector<size_t> chunkBegin(static_cast<std::size_t>(workerCount), 0);
    std::vector<size_t> chunkEnd(static_cast<std::size_t>(workerCount), 0);
    std::uint32_t nextVertexBase = 0;
    for (int w = 0; w < workerCount; ++w) {
        const size_t begin = cellCount * static_cast<size_t>(w) / static_cast<size_t>(workerCount);
        const size_t end = cellCount * static_cast<size_t>(w + 1) / static_cast<size_t>(workerCount);
        chunkBegin[static_cast<std::size_t>(w)] = begin;
        chunkEnd[static_cast<std::size_t>(w)] = end;
        chunkVertexBase[static_cast<std::size_t>(w)] = nextVertexBase;
        std::size_t chunkTriangles = 0;
        for (size_t cid = begin; cid < end; ++cid) {
            chunkTriangles += cellTriangles[cid];
        }
        nextVertexBase += static_cast<std::uint32_t>(chunkTriangles * 3u);
    }

    if (workerCount == 1) {
        chunks[0] = buildRange(0, cellCount, 0u);
        buildMs = phaseTimer.nsecsElapsed() / 1000000.0;
        phaseTimer.restart();
        if (!directWrite) M = std::move(chunks[0].mesh);
        else M.beachTriCount = chunks[0].mesh.beachTriCount;
        for (EdgeInsertion& insertion : chunks[0].edges) {
            insertEdgeSide(reg, std::move(insertion));
        }
    }
    else {
        std::vector<QFuture<void>> futures;
        futures.reserve(static_cast<std::size_t>(workerCount));
        for (int w = 0; w < workerCount; ++w) {
            const size_t begin = chunkBegin[static_cast<std::size_t>(w)];
            const size_t end = chunkEnd[static_cast<std::size_t>(w)];
            const std::uint32_t vertexBase = chunkVertexBase[static_cast<std::size_t>(w)];
            futures.push_back(QtConcurrent::run(
                QThreadPool::globalInstance(),
                [&, w, begin, end, vertexBase]() {
                    chunks[static_cast<std::size_t>(w)] = buildRange(begin, end, vertexBase);
                }));
        }
        for (QFuture<void>& future : futures) {
            future.waitForFinished();
        }
        buildMs = phaseTimer.nsecsElapsed() / 1000000.0;
        phaseTimer.restart();

        if (!directWrite) {
            M.pos.reserve(totalTriangles * 9u);
            M.col.reserve(totalTriangles * 9u);
            M.norm.reserve(totalTriangles * 9u);
            M.ore.reserve(totalTriangles * 6u);
            M.surface.reserve(totalTriangles * 12u);
            M.idx.reserve(totalTriangles * 3u);
            M.triOwner.reserve(totalTriangles);
            M.triSurfaceRole.reserve(totalTriangles);
        }
        for (CellChunk& chunk : chunks) {
            if (!directWrite) {
                M.pos.insert(M.pos.end(), chunk.mesh.pos.begin(), chunk.mesh.pos.end());
                M.col.insert(M.col.end(), chunk.mesh.col.begin(), chunk.mesh.col.end());
                M.norm.insert(M.norm.end(), chunk.mesh.norm.begin(), chunk.mesh.norm.end());
                M.ore.insert(M.ore.end(), chunk.mesh.ore.begin(), chunk.mesh.ore.end());
                M.surface.insert(M.surface.end(), chunk.mesh.surface.begin(), chunk.mesh.surface.end());
                M.idx.insert(M.idx.end(), chunk.mesh.idx.begin(), chunk.mesh.idx.end());
                M.triOwner.insert(M.triOwner.end(), chunk.mesh.triOwner.begin(), chunk.mesh.triOwner.end());
                M.triSurfaceRole.insert(
                    M.triSurfaceRole.end(), chunk.mesh.triSurfaceRole.begin(), chunk.mesh.triSurfaceRole.end());
            }
            M.beachTriCount += chunk.mesh.beachTriCount;
            for (EdgeInsertion& insertion : chunk.edges) {
                insertEdgeSide(reg, std::move(insertion));
            }
        }
    }

    mergeMs = phaseTimer.nsecsElapsed() / 1000000.0;
    phaseTimer.restart();
    if (doEdgeCliffs) {
        MeshBuilder cliffBuilder{ M.pos, M.col, M.norm, M.ore, M.surface, M.idx };
        cliffBuilder.owner = &M.triOwner;
        cliffBuilder.surfaceRole = &M.triSurfaceRole;
        cliffBuilder.beachTriCount = &M.beachTriCount;
        cliffBuilder.cells = &cells;
        if (directWrite) cliffBuilder.setDirectRange(mainTriangles, totalTriangles);
        finalizeCliffs(reg, cliffBuilder, cells);
        if (directWrite) resizeMesh(M, cliffBuilder.triangleCursor);
    }
    const double cliffsMs = phaseTimer.nsecsElapsed() / 1000000.0;
    phaseTimer.restart();

    // Per-cell triangle ranges are needed by river mesh sampling. Building the
    // CSR once here avoids rescanning millions of triangles during upload.
    M.cellTriStart.assign(cellCount + 1u, 0u);
    for (int owner : M.triOwner) {
        if (owner >= 0 && owner < static_cast<int>(cellCount)) {
            ++M.cellTriStart[static_cast<std::size_t>(owner) + 1u];
        }
    }
    for (std::size_t cellId = 0; cellId < cellCount; ++cellId) {
        M.cellTriStart[cellId + 1u] += M.cellTriStart[cellId];
    }
    M.triByCell.resize(M.cellTriStart.back());
    std::vector<std::uint32_t> cursor(M.cellTriStart.begin(), M.cellTriStart.end() - 1);
    for (std::size_t tri = 0; tri < M.triOwner.size(); ++tri) {
        const int owner = M.triOwner[tri];
        if (owner >= 0 && owner < static_cast<int>(cellCount)) {
            M.triByCell[cursor[static_cast<std::size_t>(owner)]++] = static_cast<std::uint32_t>(tri);
        }
    }

    surfaceModel_ = nullptr;
    qInfo().nospace() << "[Perf][Generation] stage=tessellation_phases cells=" << cellCount
        << " direct_write=" << directWrite
        << " workers=" << workerCount << " main_triangles=" << mainTriangles
        << " nonflat_edges=" << nonFlatEdges << " count_ms=" << countMs
        << " allocate_ms=" << allocateMs
        << " build_ms=" << buildMs << " merge_ms=" << mergeMs
        << " cliffs_ms=" << cliffsMs << " cell_index_ms=" << phaseTimer.nsecsElapsed() / 1000000.0;
    return M;
}

void TerrainTessellator::updateOreData(TerrainMesh& mesh, const HexSphereModel& model) {
    const auto& cells = model.cells();

    // Проходим по всем треугольникам
    for (size_t tri = 0; tri < mesh.triOwner.size(); ++tri) {
        int ownerId = mesh.triOwner[tri];
        if (ownerId < 0 || ownerId >= static_cast<int>(cells.size())) {
            continue;
        }

        const Cell& cell = cells[static_cast<size_t>(ownerId)];

        float oreDensity = 0.0f;
        float oreType = 0.0f;

        if (cell.biome == Biome::Rock && cell.oreType != OreType::None && cell.oreDensity > 0.0f) {
            oreDensity = std::clamp(cell.oreDensity, 0.0f, 1.0f);
            oreType = static_cast<float>(cell.oreType);
        }

        // Обновляем ore-данные для 3 вершин треугольника
        size_t baseIndex = tri * 6;  // 2 float'а на вершину * 3 вершины
        if (baseIndex + 5 < mesh.ore.size()) {
            mesh.ore[baseIndex + 0] = oreDensity;
            mesh.ore[baseIndex + 1] = oreType;
            mesh.ore[baseIndex + 2] = oreDensity;
            mesh.ore[baseIndex + 3] = oreType;
            mesh.ore[baseIndex + 4] = oreDensity;
            mesh.ore[baseIndex + 5] = oreType;
        }
    }
}

