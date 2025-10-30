#include "HexSphereModel.h"
#include <cmath>
#include <algorithm>

// Icosahedron base vertices/faces
std::pair<std::vector<QVector3D>, std::vector<Tri>> IcosphereBuilder::baseIcosahedron() {
    // Golden ratio
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<QVector3D> v = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
    };
    for (auto& p : v) p.normalize();

    std::vector<Tri> f = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };
    return { v,f };
}

IcoMesh IcosphereBuilder::build(int level) const {
    auto [P0, F0] = baseIcosahedron();
    std::vector<QVector3D> P = P0;
    std::vector<Tri> F = F0;

    std::unordered_map<EdgeKey, int, EdgeKeyHash> midpoint;
    midpoint.reserve(F.size() * 3);

    auto midpointIndex = [&](int i, int j)->int {
        EdgeKey ek(i, j);
        auto it = midpoint.find(ek);
        if (it != midpoint.end()) return it->second;
        QVector3D m = (P[i] + P[j]) * 0.5f;
        m.normalize();
        int idx = (int)P.size();
        P.push_back(m);
        midpoint.emplace(ek, idx);
        return idx;
        };

    for (int L = 0; L < level; ++L) {
        std::vector<Tri> Fnext; Fnext.reserve(F.size() * 4);
        midpoint.clear(); midpoint.reserve(F.size() * 6);
        for (const auto& t : F) {
            int a = t.a, b = t.b, c = t.c;
            int ab = midpointIndex(a, b);
            int bc = midpointIndex(b, c);
            int ca = midpointIndex(c, a);
            Fnext.push_back({ a,ab,ca });
            Fnext.push_back({ b,bc,ab });
            Fnext.push_back({ c,ca,bc });
            Fnext.push_back({ ab,bc,ca });
        }
        F.swap(Fnext);
    }

    IcoMesh mesh; mesh.P = std::move(P); mesh.F = std::move(F);
    mesh.Fv.reserve(mesh.F.size());
    for (const auto& t : mesh.F) mesh.Fv.push_back({ t.a,t.b,t.c });

    mesh.incidentFaces.assign(mesh.P.size(), {});
    for (int fi = 0; fi < (int)mesh.F.size(); ++fi) {
        const auto& t = mesh.F[fi];
        mesh.incidentFaces[t.a].push_back(fi);
        mesh.incidentFaces[t.b].push_back(fi);
        mesh.incidentFaces[t.c].push_back(fi);
    }
    return mesh;
}

static inline QVector3D triCenter(const QVector3D& a, const QVector3D& b, const QVector3D& c) {
    QVector3D m = (a + b + c) / 3.0f; m.normalize(); return m;
}

void HexSphereModel::rebuildFromIcosphere(const IcoMesh& ico) {
    L_ = 0; // we don't store level; optional
    // 1) Dual vertices: one per primal triangle (center on sphere)
    dualVerts_.resize(ico.F.size());
    for (int i = 0; i < (int)ico.F.size(); ++i) {
        const auto& t = ico.F[i];
        dualVerts_[i] = triCenter(ico.P[t.a], ico.P[t.b], ico.P[t.c]);
    }

    dualOwners_.resize(ico.F.size());
    for (int f = 0; f < (int)ico.F.size(); ++f) {
        const auto& t = ico.F[f];
        dualOwners_[f] = { t.a, t.b, t.c }; // эти индексы и есть id клеток
    }

    // 2) Build cells: one per primal vertex
    cells_.clear(); cells_.resize(ico.P.size());
    pentCount_ = 0;
    for (int v = 0; v < (int)ico.P.size(); ++v) {
        auto& cell = cells_[v]; cell.id = v;
        const auto& faces = ico.incidentFaces[v];
        const QVector3D n = ico.P[v].normalized();
        // Build a tangent frame (u,v) around normal n
        QVector3D u = std::abs(QVector3D::dotProduct(n, QVector3D(0, 1, 0))) < 0.9f ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0);
        u = QVector3D::crossProduct(u, n).normalized();
        QVector3D v2 = QVector3D::crossProduct(n, u);
        struct AngFace { float ang; int f; };
        std::vector<AngFace> angs; angs.reserve(faces.size());
        for (int f : faces) {
            const auto& tr = ico.F[f];
            QVector3D c = triCenter(ico.P[tr.a], ico.P[tr.b], ico.P[tr.c]);
            // Project to tangent plane to compute angle
            float x = QVector3D::dotProduct(c, u);
            float y = QVector3D::dotProduct(c, v2);
            float ang = std::atan2(y, x);
            angs.push_back({ ang, f });
        }
        std::sort(angs.begin(), angs.end(), [](const AngFace& a, const AngFace& b) {return a.ang < b.ang; });
        cell.poly.reserve(angs.size());
        for (auto& af : angs) cell.poly.push_back(af.f); // dual vertex index == face index
        cell.isPentagon = (cell.poly.size() == 5);
        if (cell.isPentagon) ++pentCount_;

        // Neighbors in CCW order: for each consecutive pair of faces around v, find the opposite vertex
        cell.neighbors.reserve(cell.poly.size());
        auto faceHasV = [&](const Tri& t, int vid) { return t.a == vid || t.b == vid || t.c == vid; };
        
        for (size_t i = 0; i < cell.poly.size(); ++i) {
            int f0 = cell.poly[i];
            int f1 = cell.poly[(i + 1) % cell.poly.size()];
            const Tri& T0 = ico.F[f0];
            const Tri& T1 = ico.F[f1];
            // Find the shared edge that includes primal vertex v and some neighbor w
            // T0 and T1 both contain v; find the other common vertex between T0 and T1 (besides v)
            int commonOther = -1;
            int t0v[3] = { T0.a,T0.b,T0.c };
            int t1v[3] = { T1.a,T1.b,T1.c };
            for (int ii = 0; ii < 3; ++ii) if (t0v[ii] != v) {
                for (int jj = 0; jj < 3; ++jj) if (t1v[jj] == t0v[ii] && t1v[jj] != v) {
                    commonOther = t0v[ii]; break;
                }
                if (commonOther != -1) break;
            }
            cell.neighbors.push_back(commonOther);
        }

        // Centroid (on sphere)
        QVector3D sum(0, 0, 0);
        for (int f : cell.poly) sum += dualVerts_[f];
        if (!cell.poly.empty()) sum /= float(cell.poly.size());
        if (!sum.isNull()) sum.normalize();
        cell.centroid = sum;
    }

    // 3) Build unique wire edges of the dual mesh
    std::unordered_set<EdgeKey, EdgeKeyHash> E;
    E.reserve(cells_.size() * 6);
    for (const auto& cell : cells_) {
        for (size_t i = 0; i < cell.poly.size(); ++i) {
            int a = cell.poly[i];
            int b = cell.poly[(i + 1) % cell.poly.size()];
            EdgeKey ek(a, b); E.insert(ek);
        }
    }
    wireEdges_.clear(); wireEdges_.reserve(E.size());
    for (const auto& ek : E) {
        int a = int(ek.k >> 32);
        int b = int(ek.k & 0xffffffffu);
        wireEdges_.push_back({ a,b });
    }

    // 4) Build triangle fans for picking (center + edges) per cell
    pickTris_.clear();
    for (const auto& cell : cells_) {
        if (cell.poly.size() < 3) continue;
        QVector3D center(0, 0, 0);
        for (int f : cell.poly) center += dualVerts_[f];
        center /= float(cell.poly.size());
        if (!center.isNull()) center.normalize();
        for (size_t i = 0; i < cell.poly.size(); ++i) {
            int i0 = cell.poly[i];
            int i1 = cell.poly[(i + 1) % cell.poly.size()];
            PickTri pt; pt.cellId = cell.id; pt.v0 = center; pt.v1 = dualVerts_[i0]; pt.v2 = dualVerts_[i1];
            pickTris_.push_back(pt);
        }
    }
}

void HexSphereModel::setHeight(int cellId, int h) {
    if (cellId < 0 || cellId >= static_cast<int>(cells_.size()))
        return; // защита от некорректного индекса
    cells_[cellId].height = h;
}

void HexSphereModel::addHeight(int cellId, int dh) {
    if (cellId < 0 || cellId >= static_cast<int>(cells_.size()))
        return;
    cells_[cellId].height += dh;
}

void HexSphereModel::setBiome(int cellId, Biome b) {
    if (cellId < 0 || cellId >= static_cast<int>(cells_.size()))
        return;
    cells_[cellId].biome = b;
}