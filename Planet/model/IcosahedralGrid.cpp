#include "model/IcosahedralGrid.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {
using Point = IcosahedralGrid::Point;
using Address = IcosahedralGrid::Address;

Point add(Point a, Point b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point sub(Point a, Point b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point mul(Point a, double b) { return {a.x * b, a.y * b, a.z * b}; }
double dot(Point a, Point b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Point cross(Point a, Point b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

struct Face {
    std::array<int, 3> vertices;
    Point normal;
    std::array<Point, 3> coordinates;
    std::array<int, 3> oppositeEdges{};
};

struct Base {
    std::array<Point, 12> vertices;
    std::array<Face, 20> faces;
    std::array<std::pair<int, int>, 30> edges{};
    std::array<int,12> vertexFaces{};
    std::array<int,12> vertexSlots{};
    std::array<int,30> edgeFaces{};
    std::array<std::pair<int,int>,30> edgeSlots{};

    Base() {
        const double t = (1.0 + std::sqrt(5.0)) * 0.5;
        vertices = {{{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},
            {0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},
            {t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}}};
        for (auto& p : vertices) p = IcosahedralGrid::normalized(p);
        constexpr std::array<std::array<int, 3>, 20> indices = {{{0,11,5},{0,5,1},
            {0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},
            {7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},
            {2,4,11},{6,2,10},{8,6,7},{9,8,1}}};
        int edgeCount = 0;
        for (int a = 0; a < 12; ++a) {
            for (int b = a + 1; b < 12; ++b) {
                bool present = false;
                for (const auto& f : indices) {
                    present |= std::find(f.begin(), f.end(), a) != f.end()
                        && std::find(f.begin(), f.end(), b) != f.end();
                }
                if (present) edges[edgeCount++] = {a, b};
            }
        }
        if (edgeCount != 30) throw std::logic_error("invalid base icosahedron");
        vertexFaces.fill(-1);
        edgeFaces.fill(-1);
        for (int k = 0; k < 20; ++k) {
            Face& f = faces[k];
            f.vertices = indices[k];
            const Point a = vertices[f.vertices[0]], b = vertices[f.vertices[1]], c = vertices[f.vertices[2]];
            f.normal = IcosahedralGrid::normalized(add(add(a, b), c));
            const double det = dot(a, cross(b, c));
            f.coordinates = {mul(cross(b, c), 1.0 / det),
                mul(cross(c, a), 1.0 / det), mul(cross(a, b), 1.0 / det)};
            for (int v = 0; v < 3; ++v) {
                if (vertexFaces[f.vertices[v]] < 0) {
                    vertexFaces[f.vertices[v]] = k;
                    vertexSlots[f.vertices[v]] = v;
                }
                const auto edge = std::minmax(f.vertices[(v + 1) % 3], f.vertices[(v + 2) % 3]);
                for (int e = 0; e < 30; ++e) {
                    if (edges[e].first != edge.first || edges[e].second != edge.second) continue;
                    f.oppositeEdges[v] = e;
                    if (edgeFaces[e] < 0) {
                        edgeFaces[e] = k;
                        for (int s = 0; s < 3; ++s) {
                            if (f.vertices[s] == edge.first) edgeSlots[e].first = s;
                            if (f.vertices[s] == edge.second) edgeSlots[e].second = s;
                        }
                    }
                }
            }
        }
    }
};

const Base& base() { static const Base value; return value; }
constexpr std::array<std::pair<int, int>, 6> offsets = {{{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}}};

int containingFace(Point p) {
    // The 20 face normals are the dodecahedron vertices, all with the same
    // length: (+/-1,+/-1,+/-1) and the three signed permutations below.
    // Signs eliminate opposite faces; four affine expressions select the
    // remaining symmetry type. No loop over base faces is needed.
    const double phi = (1.0 + std::sqrt(5.0)) * 0.5;
    const double inversePhi = phi - 1.0;
    const double x = std::abs(p.x), y = std::abs(p.y), z = std::abs(p.z);
    constexpr std::array<int,8> cube = {5,9,10,13,0,3,16,17};
    constexpr std::array<int,4> zeroX = {1,2,11,12};
    constexpr std::array<int,4> zeroY = {15,18,6,8};
    constexpr std::array<int,4> zeroZ = {19,14,4,7};
    const int sx = p.x < 0.0, sy = p.y < 0.0, sz = p.z < 0.0;
    int face = cube[4*sx+2*sy+sz];
    double score = x+y+z;
    const double a = phi*y+inversePhi*z;
    if (a > score) { score = a; face = zeroX[2*sy+sz]; }
    const double b = inversePhi*x+phi*z;
    if (b > score) { score = b; face = zeroY[2*sx+sz]; }
    const double c = phi*x+inversePhi*y;
    if (c > score) face = zeroZ[2*sx+sy];
    return face;
}

double bisectorSide(Point q, Point a, Point b) {
    // |q-b|^2 - |q-a|^2. For unit sites its sign is q.(a-b).
    // This factorization avoids subtracting nearly equal dot products near
    // small cells, without computing angles or enumerating candidate distances.
    return dot(sub(a,b), sub(sub(mul(q,2.0),a),b));
}

Address fromWeights(int face, const std::array<int, 3>& w) { return {face, w[1], w[2]}; }

std::uint64_t rowStart(std::uint64_t row, std::uint64_t n) {
    return row * (2 * n - 3 - row) / 2;
}
} // namespace

IcosahedralGrid::IcosahedralGrid(int frequency) : frequency_(frequency) {
    if (frequency < 1 || frequency > kMaxFrequency) throw std::invalid_argument("icosahedral frequency is out of range");
    (void)base();
}

IcosahedralGrid::Point IcosahedralGrid::normalized(Point p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        throw std::invalid_argument("sphere direction must be finite");
    const double scale = std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)});
    if (scale == 0.0) throw std::invalid_argument("sphere direction must be nonzero");
    p = {p.x / scale, p.y / scale, p.z / scale};
    return mul(p, 1.0 / std::sqrt(dot(p, p)));
}

double IcosahedralGrid::squaredChordDistance(Point a, Point b) noexcept {
    const Point d = sub(a, b);
    return dot(d, d);
}

std::uint64_t IcosahedralGrid::cellCount() const noexcept {
    return 10 * std::uint64_t(frequency_) * frequency_ + 2;
}

bool IcosahedralGrid::valid(Address a) const noexcept {
    return a.face >= 0 && a.face < 20 && a.i >= 0 && a.j >= 0
        && a.i <= frequency_ && a.j <= frequency_ - a.i;
}

std::uint64_t IcosahedralGrid::id(Address a) const {
    if (!valid(a)) throw std::invalid_argument("invalid icosahedral address");
    const Face& f = base().faces[a.face];
    const std::array<int, 3> w = {frequency_ - a.i - a.j, a.i, a.j};
    for (int k = 0; k < 3; ++k) if (w[k] == frequency_) return f.vertices[k];
    for (int k = 0; k < 3; ++k) {
        if (w[k] != 0) continue;
        const int e = f.oppositeEdges[k];
        const int higher = base().edges[e].second;
        int step = 0;
        for (int v = 0; v < 3; ++v) if (f.vertices[v] == higher) step = w[v];
        return 12 + std::uint64_t(e) * (frequency_ - 1) + step - 1;
    }
    const std::uint64_t faceSize = std::uint64_t(frequency_ - 1) * (frequency_ - 2) / 2;
    return 12 + 30 * std::uint64_t(frequency_ - 1) + a.face * faceSize
        + rowStart(a.i - 1, frequency_) + a.j - 1;
}

IcosahedralGrid::Address IcosahedralGrid::address(std::uint64_t value) const {
    if (value >= cellCount()) throw std::out_of_range("icosahedral cell ID is out of range");
    const auto& data = base();
    if (value < 12) {
        std::array<int,3> w{};
        w[data.vertexSlots[value]] = frequency_;
        return fromWeights(data.vertexFaces[value], w);
    }
    value -= 12;
    const std::uint64_t edgeSize = 30 * std::uint64_t(frequency_ - 1);
    if (value < edgeSize) {
        const auto edge = value / (frequency_ - 1);
        const auto [lo, hi] = data.edgeSlots[edge];
        const int step = int(value % (frequency_ - 1)) + 1;
        std::array<int,3> w{};
        w[lo] = frequency_ - step; w[hi] = step;
        return fromWeights(data.edgeFaces[edge], w);
    }
    value -= edgeSize;
    const std::uint64_t faceSize = std::uint64_t(frequency_ - 1) * (frequency_ - 2) / 2;
    const int face = int(value / faceSize);
    const std::uint64_t rank = value % faceSize;
    const double width = 2.0 * frequency_ - 3.0;
    // Rationalized quadratic root avoids cancellation for the first rows.
    int row = int(4.0 * double(rank) / (width + std::sqrt(width * width - 8.0 * double(rank))));
    if (row > 0 && rowStart(row, frequency_) > rank) --row;
    if (row + 1 <= frequency_ - 3 && rowStart(row + 1, frequency_) <= rank) ++row;
    return {face, row + 1, int(rank - rowStart(row, frequency_)) + 1};
}

IcosahedralGrid::Address IcosahedralGrid::canonical(Address a) const {
    if (!valid(a)) throw std::invalid_argument("invalid icosahedral address");
    if (a.i > 0 && a.j > 0 && a.i+a.j < frequency_) return a;
    return address(id(a));
}

IcosahedralGrid::Point IcosahedralGrid::pointOnFace(Address a) const {
    const auto& f = base().faces[a.face];
    const auto& vertices = base().vertices;
    return normalized(add(add(mul(vertices[f.vertices[0]], frequency_ - a.i - a.j),
        mul(vertices[f.vertices[1]], a.i)), mul(vertices[f.vertices[2]], a.j)));
}

IcosahedralGrid::Point IcosahedralGrid::point(Address a) const { return pointOnFace(canonical(a)); }

IcosahedralGrid::Address IcosahedralGrid::locate(Point direction) const {
    const Point q = normalized(direction);
    const int face = containingFace(q);
    const auto& frame = base().faces[face];
    std::array<double, 3> w{};
    for (int k = 0; k < 3; ++k) w[k] = std::max(0.0, dot(frame.coordinates[k], q));
    const double scale = frequency_ / (w[0] + w[1] + w[2]);
    const double u = std::clamp(w[1] * scale, 0.0, double(frequency_));
    const double v = std::clamp(w[2] * scale, 0.0, double(frequency_));
    int i = std::min(int(std::floor(u)), frequency_);
    int j = std::min(int(std::floor(v)), frequency_ - i);
    // Boundary points select an incident, valid triangle inside the base face.
    const bool boundary = i+j == frequency_;
    if (boundary) { if (j > 0) --j; else --i; }
    const bool lower = boundary || i+j == frequency_-1 || (u-i)+(v-j) <= 1.0;
    const std::array<Address, 3> triangle = lower
        ? std::array<Address, 3>{{{face,i,j},{face,i+1,j},{face,i,j+1}}}
        : std::array<Address, 3>{{{face,i+1,j},{face,i+1,j+1},{face,i,j+1}}};
    const Point a = pointOnFace(triangle[0]);
    const Point b = pointOnFace(triangle[1]);
    const Point c = pointOnFace(triangle[2]);
    // Each acute Delaunay triangle is partitioned by its bisectors into the
    // three incident Voronoi cells. Two side tests give the cell directly.
    if (bisectorSide(q,a,b) >= 0.0)
        return canonical(bisectorSide(q,a,c) >= 0.0 ? triangle[0] : triangle[2]);
    return canonical(bisectorSide(q,b,c) >= 0.0 ? triangle[1] : triangle[2]);
}

std::optional<IcosahedralGrid::Address> IcosahedralGrid::locateRay(Point origin, Point direction, double radius) const {
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)
        || !std::isfinite(radius) || radius <= 0.0)
        throw std::invalid_argument("ray origin and positive sphere radius must be finite");
    const Point d = normalized(direction);
    const double along = dot(origin, d);
    const Point perpendicular = sub(origin, mul(d, along));
    const double offset = std::hypot(perpendicular.x, perpendicular.y, perpendicular.z);
    if (offset > radius) return std::nullopt;
    const double ratio = offset / radius;
    const double root = radius * std::sqrt(std::max(0.0, (1.0 - ratio) * (1.0 + ratio)));
    if (-along - root >= 0.0) return locate(sub(perpendicular, mul(d, root)));
    if (-along + root >= 0.0) return locate(add(perpendicular, mul(d, root)));
    return std::nullopt;
}

IcosahedralGrid::IncidentTriangles IcosahedralGrid::incidentTriangles(Address a) const {
    if (!valid(a)) throw std::invalid_argument("invalid icosahedral address");
    const auto& data = base();
    const auto& source = data.faces[a.face].vertices;
    const std::array<int, 3> w = {frequency_ - a.i - a.j, a.i, a.j};
    IncidentTriangles result;
    a = canonical(a);
    for (int f = 0; f < 20; ++f) {
        std::array<int, 3> target{};
        bool compatible = true;
        for (int k = 0; k < 3; ++k) {
            if (w[k] == 0) continue;
            auto it = std::find(data.faces[f].vertices.begin(), data.faces[f].vertices.end(), source[k]);
            if (it == data.faces[f].vertices.end()) { compatible = false; break; }
            target[std::size_t(it - data.faces[f].vertices.begin())] = w[k];
        }
        if (!compatible) continue;
        const Address center = fromWeights(f, target);
        for (int k = 0; k < 6; ++k) {
            const auto [di, dj] = offsets[k];
            const auto [ei, ej] = offsets[(k + 1) % 6];
            Address b{f, center.i + di, center.j + dj}, c{f, center.i + ei, center.j + ej};
            if (!valid(b) || !valid(c)) continue;
            if (result.count == 6) throw std::logic_error("too many incident grid triangles");
            result.triangles[result.count++] = {a, canonical(b), canonical(c)};
        }
    }
    return result;
}

IcosahedralGrid::Neighbors IcosahedralGrid::neighbors(Address a) const {
    const auto incident = incidentTriangles(a);
    Neighbors result;
    for (int k = 0; k < incident.count; ++k) result.addresses[result.count++] = incident.triangles[k].b;
    return result;
}

IcosahedralGrid::Cell IcosahedralGrid::cell(Address a) const {
    const auto incident = incidentTriangles(a);
    Cell result;
    result.generator = point(a);
    result.degree = incident.count;
    int current = 0;
    std::array<bool, 6> used{};
    for (int k = 0; k < result.degree; ++k) {
        if (used[current]) throw std::logic_error("invalid grid triangle ring");
        used[current] = true;
        const auto& t = incident.triangles[current];
        result.vertices[k] = dualVertex(t.a, t.b, t.c);
        result.neighbors[k] = t.c;
        int next = -1;
        for (int j = 0; j < incident.count; ++j) if (incident.triangles[j].b == t.c) { next = j; break; }
        if (next < 0) throw std::logic_error("open grid triangle ring");
        current = next;
    }
    if (current != 0) throw std::logic_error("grid triangle ring did not close");
    return result;
}

IcosahedralGrid::Point IcosahedralGrid::edgeMidpoint(Address a, Address b) const {
    return normalized(add(point(a), point(b)));
}

IcosahedralGrid::Point IcosahedralGrid::bisectorNormal(Address a, Address b) const {
    return normalized(sub(point(a), point(b)));
}

IcosahedralGrid::Point IcosahedralGrid::dualVertex(Address a, Address b, Address c) const {
    const Point pa = point(a), pb = point(b), pc = point(c);
    Point result = normalized(cross(sub(pb, pa), sub(pc, pa)));
    if (dot(result, pa) < 0.0) result = mul(result, -1.0);
    return result;
}
