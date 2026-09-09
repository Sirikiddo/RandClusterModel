#include "renderers/TerrainTessellator.h"

#include <cmath>
#include <numeric>

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
    pc.inner.resize(static_cast<size_t>(deg));
    pc.outerUnit.resize(static_cast<size_t>(deg));
    pc.h = static_cast<float>(c.height);
    pc.beachLike = isBeachLikeCell(c);
    pc.color = colorForCell(c);
    pc.center = liftUnit(c.centroid, pc.h);

    for (int i = 0; i < deg; ++i) {
        const QVector3D u = slerpish(dual[static_cast<size_t>(c.poly[static_cast<size_t>(i)])], c.centroid, inset);
        pc.inner[static_cast<size_t>(i)] = liftUnit(u, pc.h);
        pc.outerUnit[static_cast<size_t>(i)] = dual[static_cast<size_t>(c.poly[static_cast<size_t>(i)])];
    }
    return pc;
}

TerrainTessellator::TrimDirs TerrainTessellator::makeTrimDirs(const PreCell& pc) const {
    TrimDirs td;
    const int deg = static_cast<int>(pc.outerUnit.size());
    const float t = std::clamp(outerTrim, 0.0f, 0.49f);
    td.sideL.resize(static_cast<size_t>(deg));
    td.sideR.resize(static_cast<size_t>(deg));
    td.prevU.resize(static_cast<size_t>(deg));
    td.currU.resize(static_cast<size_t>(deg));
    td.apexU.resize(static_cast<size_t>(deg));

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
    eh.edgeH.resize(static_cast<size_t>(deg));
    eh.apexH.resize(static_cast<size_t>(deg));
    for (int i = 0; i < deg; ++i) {
        eh.edgeH[static_cast<size_t>(i)] = bladeHeightForEdge(c, i, cells);
        const float hBlend = cornerBlendTargetHeight(c, i, cells);
        eh.apexH[static_cast<size_t>(i)] = 0.5f * (pc.h + hBlend);
    }
    return eh;
}

void TerrainTessellator::MeshBuilder::triToward(
    QVector3D A,
    QVector3D B,
    QVector3D C,
    const QVector3D& color,
    const QVector3D& toward,
    int cellOwner,
    TriangleSurfaceRole role) {
    QVector3D n = QVector3D::crossProduct(B - A, C - A).normalized();
    QVector3D t = toward;
    if (!t.isNull()) {
        t.normalize();
    }
    if (QVector3D::dotProduct(n, t) < 0.0f) {
        std::swap(B, C);
        n = -n;
    }

    const uint32_t base = static_cast<uint32_t>(pos.size() / 3);
    pos.insert(pos.end(), { A.x(), A.y(), A.z(), B.x(), B.y(), B.z(), C.x(), C.y(), C.z() });
    col.insert(col.end(), { color.x(), color.y(), color.z(), color.x(), color.y(), color.z(), color.x(), color.y(), color.z() });
    norm.insert(norm.end(), { n.x(), n.y(), n.z(), n.x(), n.y(), n.z(), n.x(), n.y(), n.z() });

    float oreDensity = 0.0f;
    float oreType = 0.0f;
    if (cells && cellOwner >= 0 && cellOwner < static_cast<int>(cells->size())) {
        const Cell& cell = (*cells)[static_cast<size_t>(cellOwner)];
        if (cell.biome == Biome::Rock && cell.oreType != OreType::None && cell.oreDensity > 0.0f) {
            oreDensity = std::clamp(cell.oreDensity, 0.0f, 1.0f);
            oreType = static_cast<float>(cell.oreType);
        }
    }
    ore.insert(ore.end(), {
        oreDensity, oreType,
        oreDensity, oreType,
        oreDensity, oreType
    });
    idx.insert(idx.end(), { base, base + 1, base + 2 });

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
    TriangleSurfaceRole role) {
    triToward(Q0, Q1, Q2, color, toward, cellOwner, role);
    triToward(Q0, Q2, Q3, color, toward, cellOwner, role);
}

void TerrainTessellator::buildInnerFan(MeshBuilder& mb, const Cell& c, const PreCell& pc) const {
    const int deg = static_cast<int>(c.poly.size());
    for (int i = 0; i < deg; ++i) {
        const QVector3D& A = pc.inner[static_cast<size_t>(i)];
        const QVector3D& B = pc.inner[static_cast<size_t>((i + 1) % deg)];
        mb.triToward(pc.center, A, B, pc.color, pc.center, c.id);
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
        mb.quadToward(pc.inner[static_cast<size_t>(i)], pc.inner[static_cast<size_t>(j)], O1, O0, pc.color, toward, c.id);
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

        mb.triToward(I, Ocurr, Apex, pc.color, I + Ocurr + Apex, c.id);
        mb.triToward(I, Oprev, Apex, pc.color, I + Oprev + Apex, c.id);
        if (mb.beachTriCount && coastalBand && isBeachLikeCell(c) && ((coastalBand->shoreVertexMask[static_cast<size_t>(c.id)] >> i) & 1u) != 0u) {
            *mb.beachTriCount += 2;
        }
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
    const int deg = static_cast<int>(c.poly.size());
    const int j = (iEdge + 1) % deg;
    const int iPrev = (iEdge + deg - 1) % deg;

    const int dv_i = c.poly[static_cast<size_t>(iEdge)];
    const int dv_j = c.poly[static_cast<size_t>(j)];
    const EdgeKey key{ std::min(dv_i, dv_j), std::max(dv_i, dv_j) };
    const bool canon = (dv_i <= dv_j);

    EdgeSide S;
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

    auto& rec = reg[key];
    if (!rec.A) {
        rec.A = S;
    }
    else {
        rec.B = S;
    }
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

    MeshBuilder mb{ M.pos, M.col, M.norm, M.ore, M.idx };
    mb.owner = &M.triOwner;
    mb.surfaceRole = &M.triSurfaceRole;
    mb.beachTriCount = &M.beachTriCount;
    mb.cells = &cells;
    EdgeRegistry reg;

    for (size_t cid = 0; cid < cells.size(); ++cid) {
        const Cell& c = cells[cid];
        const int deg = static_cast<int>(c.poly.size());
        if (deg < 3) {
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
                registerEdgeSide(reg, cid, c, i, pc, td, eh);
            }
        }
    }

    if (doEdgeCliffs) {
        finalizeCliffs(reg, mb, cells);
    }
    surfaceModel_ = nullptr;
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
