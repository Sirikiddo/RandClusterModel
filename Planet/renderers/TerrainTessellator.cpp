#include "renderers/TerrainTessellator.h"
#include <cmath>
#include <numeric>
#include <random>

// ── базовые операции ────────────────────────────────────────────────────────
QVector3D TerrainTessellator::slerpish(const QVector3D& a, const QVector3D& b, float t) {
    QVector3D x = (1.f - t) * a + t * b;
    if (!x.isNull()) x.normalize();
    return x;
}

QVector3D TerrainTessellator::liftUnit(const QVector3D& unit, float h) const {
    QVector3D n = unit; if (!n.isNull()) n.normalize();
    return n * (R + h * heightStep);
}

TerrainTessellator::EdgeMode
TerrainTessellator::classifyEdge(int hA, int hB, int k) {
    const int d = std::abs(hA - hB);
    if (d == 0) return EdgeMode::Flat;
    if (d <= k) return EdgeMode::Slope;
    return EdgeMode::Cliff;
}

int TerrainTessellator::findLocalIndex(const Cell& c, int dv) {
    for (int i = 0; i < (int)c.poly.size(); ++i) if (c.poly[i] == dv) return i;
    return -1;
}

// ── высоты для рёбер/углов ──────────────────────────────────────────────────
float TerrainTessellator::bladeHeightForEdge(const Cell& c, int edgeIdx,
    const std::vector<Cell>& cells) const {
    const int nId = c.neighbors[edgeIdx];
    if (nId >= 0 && classifyEdge(c.height, cells[size_t(nId)].height, smoothMaxDelta) == EdgeMode::Slope)
        return 0.5f * (float)c.height + 0.5f * (float)cells[size_t(nId)].height;
    return float(c.height);
}

float TerrainTessellator::cornerBlendTargetHeight(const Cell& c, int i,
    const std::vector<Cell>& cells) const {
    const int deg = (int)c.poly.size();
    const int iPrev = (i + deg - 1) % deg;

    const int nL = c.neighbors[iPrev], nR = c.neighbors[i];
    const bool hasL = nL >= 0, hasR = nR >= 0;
    const int  hL = hasL ? cells[size_t(nL)].height : c.height;
    const int  hR = hasR ? cells[size_t(nR)].height : c.height;

    const bool smL = hasL && (classifyEdge(c.height, hL, smoothMaxDelta) == EdgeMode::Slope);
    const bool smR = hasR && (classifyEdge(c.height, hR, smoothMaxDelta) == EdgeMode::Slope);

    if (smL && smR) return 0.5f * (float(hL) + float(hR));
    if (smL)        return float(hL);
    if (smR)        return float(hR);
    return float(c.height);
}

// ── реализация OreNoiseGenerator ────────────────────────────────────────────
TerrainTessellator::OreNoiseGenerator::OreNoiseGenerator(uint32_t seed) {
    std::array<int, 256> perm;
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), std::default_random_engine(seed));

    for (int i = 0; i < 256; i++) {
        perm_[i] = perm_[i + 256] = perm[i];
    }
}

float TerrainTessellator::OreNoiseGenerator::fade(float t) {
    return t * t * t * (t * (t * 6 - 15) + 10);
}

float TerrainTessellator::OreNoiseGenerator::lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float TerrainTessellator::OreNoiseGenerator::grad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float TerrainTessellator::OreNoiseGenerator::noise(float x, float y, float z, float time) const {
    int xi = (int)std::floor(x) & 255;
    int yi = (int)std::floor(y) & 255;
    int zi = (int)std::floor(z + time) & 255;

    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float zf = z - std::floor(z);

    float u = fade(xf);
    float v = fade(yf);
    float w = fade(zf);

    int a = perm_[xi] + yi;
    int aa = perm_[a] + zi;
    int ab = perm_[a + 1] + zi;
    int b = perm_[xi + 1] + yi;
    int ba = perm_[b] + zi;
    int bb = perm_[b + 1] + zi;

    float x1 = lerp(grad(perm_[aa], xf, yf, zf),
        grad(perm_[ba], xf - 1, yf, zf),
        u);
    float x2 = lerp(grad(perm_[ab], xf, yf - 1, zf),
        grad(perm_[bb], xf - 1, yf - 1, zf),
        u);
    float y1 = lerp(x1, x2, v);

    float x3 = lerp(grad(perm_[aa + 1], xf, yf, zf - 1),
        grad(perm_[ba + 1], xf - 1, yf, zf - 1),
        u);
    float x4 = lerp(grad(perm_[ab + 1], xf, yf - 1, zf - 1),
        grad(perm_[bb + 1], xf - 1, yf - 1, zf - 1),
        u);
    float y2 = lerp(x3, x4, v);

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
    const QVector3D& position) const
{
    if (!enableOreVisualization || cell.oreDensity < 0.01f) {
        return baseColor;
    }

    // Генерируем визуальные параметры руды на основе ее типа и плотности
    float grainSize = 0.02f + cell.oreDensity * 0.08f;
    float grainContrast = 1.0f + cell.oreDensity * 2.0f;

    // Определяем цвет руды на основе типа
    QVector3D oreColor;
    switch (cell.oreType) {
    case 1: // Железо
        oreColor = QVector3D(0.7f, 0.4f, 0.2f);
        grainSize *= 1.2f;
        break;
    case 2: // Медь
        oreColor = QVector3D(0.8f, 0.5f, 0.2f);
        grainContrast *= 1.5f;
        break;
    case 3: // Золото
        oreColor = QVector3D(0.9f, 0.9f, 0.1f);
        grainSize *= 0.8f;
        grainContrast *= 2.0f;
        break;
    default:
        oreColor = QVector3D(0.5f, 0.5f, 0.5f);
    }

    // Масштаб для шума в зависимости от плотности руды
    float noiseScale = 10.0f + cell.oreDensity * 50.0f;

    // Генерируем шум для зернистости
    float noise1 = oreNoise_.noise(
        position.x() * noiseScale,
        position.y() * noiseScale,
        position.z() * noiseScale,
        animationTime_
    );

    float noise2 = oreNoise_.noise(
        position.x() * noiseScale * 2.3f,
        position.y() * noiseScale * 2.3f,
        position.z() * noiseScale * 2.3f,
        animationTime_ * 0.7f
    );

    // Комбинируем шумы для более сложной текстуры
    float combinedNoise = (noise1 * 0.7f + noise2 * 0.3f);

    // Определяем, является ли эта точка "зерном" руды
    float grainThreshold = 0.5f + cell.oreDensity * 0.3f;
    float grainValue = std::sin(combinedNoise * 3.14159f * grainSize * 100.0f);

    if (grainValue > grainThreshold) {
        // Это зерно руды - используем цвет зерна
        float grainIntensity = (grainValue - grainThreshold) / (1.0f - grainThreshold);
        grainIntensity = std::pow(grainIntensity, grainContrast);

        // Цвет зерна - немного темнее/светлее основного цвета руды
        QVector3D grainColor = oreColor * (0.8f + (cell.oreDensity * 0.4f));

        // Интерполяция между базовым цветом и цветом зерна
        float oreInfluence = grainIntensity * cell.oreDensity;
        return baseColor * (1.0f - oreInfluence) + grainColor * oreInfluence;
    }

    // Не зерно - возможно слабое влияние цвета руды
    float influence = std::max(0.0f, grainValue - 0.3f) / 0.2f;
    if (influence > 0.0f) {
        float oreInfluence = influence * 0.2f * cell.oreDensity;
        return baseColor * (1.0f - oreInfluence) + oreColor * oreInfluence;
    }

    return baseColor;
}

// ── подготовка клетки ───────────────────────────────────────────────────────
TerrainTessellator::PreCell
TerrainTessellator::makePreCell(const Cell& c, const std::vector<QVector3D>& dual) const {
    PreCell pc;
    const int deg = (int)c.poly.size();
    pc.inner.resize(deg);
    pc.outerUnit.resize(deg);
    pc.h = float(c.height);

    // Базовый цвет биома
    QVector3D baseColor = HexSphereModel::biomeColor(c.biome, c.temperature);

    // Применяем визуализацию руды к цвету
    pc.color = calculateCellColorWithOre(c, baseColor, c.centroid);

    pc.center = liftUnit(c.centroid, pc.h);

    for (int i = 0; i < deg; ++i) {
        const QVector3D u = slerpish(dual[size_t(c.poly[i])], c.centroid, inset);
        pc.inner[i] = liftUnit(u, pc.h);
        pc.outerUnit[i] = dual[size_t(c.poly[i])];
    }
    return pc;
}

TerrainTessellator::TrimDirs
TerrainTessellator::makeTrimDirs(const PreCell& pc) const {
    TrimDirs td;
    const int deg = (int)pc.outerUnit.size();
    const float t = std::clamp(outerTrim, 0.f, 0.49f);
    td.sideL.resize(deg); td.sideR.resize(deg);
    td.prevU.resize(deg); td.currU.resize(deg); td.apexU.resize(deg);

    auto U = [&](int k)->QVector3D { return pc.outerUnit[(k % deg + deg) % deg]; };

    for (int i = 0; i < deg; ++i) {
        const int j = (i + 1) % deg;
        const int iPrev = (i + deg - 1) % deg;
        td.sideL[i] = slerpish(U(i), U(j), t);
        td.sideR[i] = slerpish(U(i), U(j), 1.f - t);
        td.prevU[i] = slerpish(U(iPrev), U(i), 1.f - t);
        td.currU[i] = slerpish(U(i), U(j), t);
        td.apexU[i] = U(i);
    }
    return td;
}

TerrainTessellator::EdgeHeights
TerrainTessellator::makeHeights(const Cell& c, const PreCell& pc,
    const std::vector<Cell>& cells) const {
    const int deg = (int)c.poly.size();
    EdgeHeights eh; eh.edgeH.resize(deg); eh.apexH.resize(deg);
    for (int i = 0; i < deg; ++i) {
        eh.edgeH[i] = bladeHeightForEdge(c, i, cells);
        const float hBlend = cornerBlendTargetHeight(c, i, cells);
        eh.apexH[i] = 0.5f * (pc.h + hBlend);
    }
    return eh;
}

// ── MeshBuilder ─────────────────────────────────────────────────────────────
void TerrainTessellator::MeshBuilder::triToward(QVector3D A, QVector3D B, QVector3D C,
    const QVector3D& color, const QVector3D& toward, int cellOwner)
{
    // Вычисляем нормаль
    QVector3D n = QVector3D::crossProduct(B - A, C - A).normalized();

    // Ориентируем треугольник наружу
    QVector3D t = toward;
    if (!t.isNull()) t.normalize();
    if (QVector3D::dotProduct(n, t) < 0.f) {
        std::swap(B, C);
        n = -n; // инвертируем нормаль при смене порядка вершин
    }

    const uint32_t base = uint32_t(pos.size() / 3);

    // Позиции
    pos.insert(pos.end(), { A.x(),A.y(),A.z(), B.x(),B.y(),B.z(), C.x(),C.y(),C.z() });

    // Цвета
    col.insert(col.end(), { color.x(),color.y(),color.z(),
                           color.x(),color.y(),color.z(),
                           color.x(),color.y(),color.z() });

    // Нормали (одинаковые для всех вершин треугольника)
    norm.insert(norm.end(), { n.x(),n.y(),n.z(), n.x(),n.y(),n.z(), n.x(),n.y(),n.z() });

    // Индексы
    idx.insert(idx.end(), { base, base + 1, base + 2 });

    if (owner) owner->push_back(cellOwner);
}

void TerrainTessellator::MeshBuilder::quadToward(const QVector3D& Q0, const QVector3D& Q1,
    const QVector3D& Q2, const QVector3D& Q3,
    const QVector3D& color, const QVector3D& toward,
    int cellOwner)
{
    triToward(Q0, Q1, Q2, color, toward, cellOwner);
    triToward(Q0, Q2, Q3, color, toward, cellOwner);
}

// ── "мягкие" этапы ──────────────────────────────────────────────────────────
void TerrainTessellator::buildInnerFan(MeshBuilder& mb, const Cell& c, const PreCell& pc) const {
    const int deg = (int)c.poly.size();
    for (int i = 0; i < deg; ++i) {
        const QVector3D& A = pc.inner[i];
        const QVector3D& B = pc.inner[(i + 1) % deg];
        mb.triToward(pc.center, A, B, pc.color, pc.center, c.id);
    }
}

void TerrainTessellator::buildBlades(MeshBuilder& mb, const Cell& c, const PreCell& pc,
    const TrimDirs& td, const EdgeHeights& eh) const {
    const int deg = (int)c.poly.size();
    for (int i = 0; i < deg; ++i) {
        const int j = (i + 1) % deg;
        const QVector3D O0 = liftUnit(td.sideL[i], eh.edgeH[i]);
        const QVector3D O1 = liftUnit(td.sideR[i], eh.edgeH[i]);
        const QVector3D toward = pc.inner[i] + pc.inner[j] + O0 + O1;
        mb.quadToward(pc.inner[i], pc.inner[j], O1, O0, pc.color, toward, c.id);
    }
}

void TerrainTessellator::buildCorners(MeshBuilder& mb, const Cell& c, const PreCell& pc,
    const TrimDirs& td, const EdgeHeights& eh) const {
    const int deg = (int)c.poly.size();
    for (int i = 0; i < deg; ++i) {
        const int iPrev = (i + deg - 1) % deg;
        const QVector3D I = pc.inner[i];
        const QVector3D Ocurr = liftUnit(td.sideL[i], eh.edgeH[i]);
        const QVector3D Oprev = liftUnit(td.sideR[iPrev], eh.edgeH[iPrev]);
        const QVector3D Apex = liftUnit(td.apexU[i], eh.apexH[i]);

        mb.triToward(I, Ocurr, Apex, pc.color, I + Ocurr + Apex, c.id);
        mb.triToward(I, Oprev, Apex, pc.color, I + Oprev + Apex, c.id);
    }
}

// ── реестр рёбер: заполнение сторон при проходе по клетке ───────────────────
void TerrainTessellator::registerEdgeSide(EdgeRegistry& reg, size_t cid,
    const Cell& c, int iEdge,
    const PreCell& pc, const TrimDirs& td, const EdgeHeights& eh) const
{
    const int deg = (int)c.poly.size();
    const int j = (iEdge + 1) % deg;
    const int iPrev = (iEdge + deg - 1) % deg;

    const int dv_i = c.poly[iEdge];
    const int dv_j = c.poly[j];
    const EdgeKey key{ std::min(dv_i, dv_j), std::max(dv_i, dv_j) };
    const bool canon = (dv_i <= dv_j);

    EdgeSide S;
    S.cellId = int(cid);
    S.hCell = c.height;
    S.Hedge = eh.edgeH[iEdge];
    S.Hleft = eh.edgeH[iPrev];
    S.Hright = eh.edgeH[iEdge];
    S.apexL = eh.apexH[iEdge];
    S.apexR = eh.apexH[j];
    S.sideL = td.sideL[iEdge];
    S.sideR = td.sideR[iEdge];
    S.prevU_L = td.prevU[iEdge];
    S.currU_R = td.currU[iEdge];
    S.apexDirL = td.apexU[iEdge];
    S.apexDirR = td.apexU[j];
    S.centroid = c.centroid;

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
    if (!rec.A) rec.A = S;
    else        rec.B = S;
}

// ── пост-проход: генерация клифов по парам сторон ───────────────────────────
void TerrainTessellator::finalizeCliffs(const EdgeRegistry& reg,
    MeshBuilder& mb,
    const std::vector<Cell>& cells) const
{
    const QVector3D cliffColor(0.55f, 0.38f, 0.25f);

    auto towardDir = [&](const EdgeSide& hi, const EdgeSide& lo) {
        QVector3D t = cells[size_t(lo.cellId)].centroid - cells[size_t(hi.cellId)].centroid;
        if (t.isNull()) t = (hi.P_edgeL + hi.P_edgeR + lo.P_edgeL + lo.P_edgeR);
        return t;
        };

    auto diff = [&](const QVector3D& a, const QVector3D& b) {
        return (a - b).lengthSquared() > (epsApex * epsApex);
        };

    for (const auto& [key, rec] : reg) {
        if (!rec.A || !rec.B) continue;
        const EdgeSide& A = *rec.A;
        const EdgeSide& B = *rec.B;

        const EdgeMode mode = classifyEdge(A.hCell, B.hCell, smoothMaxDelta);
        const bool AisHigh = (A.hCell > B.hCell);
        const EdgeSide& hi = AisHigh ? A : B;
        const EdgeSide& lo = AisHigh ? B : A;
        const QVector3D toward = towardDir(hi, lo);

        if (mode == EdgeMode::Cliff) {
            // центральный прямоугольник (общая полоса между inner-прямоугольниками)
            mb.quadToward(hi.P_edgeL, hi.P_edgeR, lo.P_edgeR, lo.P_edgeL, cliffColor, toward, hi.cellId);

            // левая трапеция (общая вершина — P_edgeL)
            mb.quadToward(hi.P_edgeL, hi.P_apexL, lo.P_apexL, lo.P_edgeL, cliffColor, toward, hi.cellId);

            // правая трапеция (общая вершина — P_edgeR)
            mb.quadToward(hi.P_edgeR, hi.P_apexR, lo.P_apexR, lo.P_edgeR, cliffColor, toward, hi.cellId);
        }
        else if (mode == EdgeMode::Slope) {
            // только если апексы отличаются — шьём угловые треугольники к общей точке полосы
            if (diff(A.P_apexL, B.P_apexL))
            {
                const bool aHigher = (A.apexL > B.apexL);
                mb.triToward(aHigher ? A.P_apexL : B.P_apexL,
                    aHigher ? A.P_edgeL : B.P_edgeL,
                    aHigher ? B.P_apexL : A.P_apexL,
                    cliffColor, toward,
                    aHigher ? A.cellId : B.cellId);
            }

            if (diff(A.P_apexR, B.P_apexR))
            {
                const bool aHigher = (A.apexR > B.apexR);
                mb.triToward(aHigher ? A.P_apexR : B.P_apexR,
                    aHigher ? A.P_edgeR : B.P_edgeR,
                    aHigher ? B.P_apexR : A.P_apexR,
                    cliffColor, toward,
                    aHigher ? A.cellId : B.cellId);
            }
        }
    }
}

// ── главный проход ──────────────────────────────────────────────────────────
TerrainMesh TerrainTessellator::build(const HexSphereModel& model) const {
    TerrainMesh M;
    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();

    // Инициализация MeshBuilder с поддержкой нормалей
    MeshBuilder mb{ M.pos, M.col, M.norm, M.idx };
    mb.owner = &M.triOwner;
    EdgeRegistry reg;

    for (size_t cid = 0; cid < cells.size(); ++cid) {
        const Cell& c = cells[cid];
        const int deg = (int)c.poly.size();
        if (deg < 3) continue;

        const PreCell    pc = makePreCell(c, dual);
        const TrimDirs   td = makeTrimDirs(pc);
        const EdgeHeights eh = makeHeights(c, pc, cells);

        if (doCaps)       buildInnerFan(mb, c, pc);
        if (doBlades)     buildBlades(mb, c, pc, td, eh);
        if (doCornerTris) buildCorners(mb, c, pc, td, eh);

        // регистрируем профиль каждой стороны ребра (для пост-прохода)
        if (doEdgeCliffs) {
            for (int i = 0; i < deg; ++i)
                registerEdgeSide(reg, cid, c, i, pc, td, eh);
        }
    }

    if (doEdgeCliffs) finalizeCliffs(reg, mb, cells);
    return M;
}