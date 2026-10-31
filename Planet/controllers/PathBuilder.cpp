#include "controllers/PathBuilder.h"

#include <algorithm>
#include <cmath>

namespace {

    float slopePenalty(int climbDelta) {
        if (climbDelta <= 0) {
            return 1.0f + float(-climbDelta) * 0.08f;
        }
        return 1.0f + float(climbDelta) * 0.35f;
    }

} // namespace

PathBuilder::PathBuilder(const HexSphereTopology& topology,
    const std::vector<TerrainCellState>& state,
    int smoothMaxDelta)
    : smoothMaxDelta_(effectiveMaxClimbDelta(smoothMaxDelta))
    , topology_(&topology)
    , state_(&state) {
}

int PathBuilder::effectiveMaxClimbDelta(int smoothMaxDelta) {
    return std::clamp(smoothMaxDelta, 0, kMaxClimbDelta);
}

void PathBuilder::setBlockedCells(const std::vector<int>& cellIds) {
    const size_t count = topology_ ? topology_->cells.size() : model_->cells().size();
    blocked_.assign(count, 0);
    for (int cellId : cellIds) {
        if (cellId >= 0 && cellId < static_cast<int>(blocked_.size())) {
            blocked_[static_cast<size_t>(cellId)] = 1;
        }
    }
}

bool PathBuilder::isBlocked(int cellId) const {
    return cellId >= 0
        && cellId < static_cast<int>(blocked_.size())
        && blocked_[static_cast<size_t>(cellId)] != 0;
}

void PathBuilder::build(PathBuilder::WeightFn w) const {
    if (topology_ && state_) {
        const int n = static_cast<int>(std::min(topology_->cells.size(), state_->size()));
        g_.assign(n, {});
        for (int u = 0; u < n; ++u) {
            const auto& from = topology_->cells[static_cast<size_t>(u)];
            for (int v : from.neighbors) {
                if (v < 0 || v >= n) continue;
                const auto& to = topology_->cells[static_cast<size_t>(v)];
                if (to.id < 0) continue;
                const auto& fs = (*state_)[static_cast<size_t>(u)];
                const auto& ts = (*state_)[static_cast<size_t>(v)];
                if (ts.biome == Biome::Sea || isBlocked(v)) continue;
                const int climb = ts.height - fs.height;
                if (std::abs(climb) > smoothMaxDelta_) continue;
                const float dot = std::clamp(QVector3D::dotProduct(
                    from.centroid.normalized(), to.centroid.normalized()), -1.0f, 1.0f);
                const float distance = std::acos(dot);
                const float terrainFactor = 0.5f * (biomeTraversalFactor(fs.biome) + biomeTraversalFactor(ts.biome));
                g_[u].push_back({v, distance * terrainFactor * slopePenalty(climb)});
            }
        }
        return;
    }

    const auto& cells = model_->cells();
    const int n = static_cast<int>(cells.size());
    g_.assign(n, {});

    for (int u = 0; u < n; ++u) {
        const auto& from = cells[static_cast<size_t>(u)];
        for (int v : from.neighbors) {
            if (v < 0) {
                continue;
            }

            const auto& to = cells[static_cast<size_t>(v)];
            const float weight = w ? w(from, to) : traversalCost(from, to);
            if (std::isfinite(weight)) {
                g_[u].push_back({ v, weight });
            }
        }
    }
}

bool PathBuilder::isTraversable(const Cell& from, const Cell& to) const {
    if (to.biome == Biome::Sea) {
        return false;
    }

    // Дерево — препятствие: дорога не проходит сквозь занятую им клетку.
    if (isBlocked(to.id)) {
        return false;
    }

    const int climbDelta = to.height - from.height;
    return std::abs(climbDelta) <= smoothMaxDelta_;
}

float PathBuilder::edgeAngularDistance(const Cell& from, const Cell& to) {
    const float dot = std::clamp(
        QVector3D::dotProduct(from.centroid.normalized(), to.centroid.normalized()),
        -1.0f,
        1.0f);
    return std::acos(dot);
}

float PathBuilder::biomeTraversalFactor(Biome biome) {
    switch (biome) {
    case Biome::Sea:     return 2.40f;
    case Biome::Grass:   return 1.00f;
    case Biome::Rock:    return 1.45f;
    case Biome::Snow:    return 1.80f;
    case Biome::Tundra:  return 1.30f;
    case Biome::Desert:  return 1.20f;
    case Biome::Savanna: return 1.10f;
    case Biome::Jungle:  return 1.55f;
    default:             return 1.20f;
    }
}

float PathBuilder::traversalCost(const Cell& from, const Cell& to) const {
    if (!isTraversable(from, to)) {
        return std::numeric_limits<float>::infinity();
    }

    const float distance = edgeAngularDistance(from, to);
    const float terrainFactor =
        0.5f * (biomeTraversalFactor(from.biome) + biomeTraversalFactor(to.biome));
    const int climbDelta = to.height - from.height;
    return distance * terrainFactor * slopePenalty(climbDelta);
}

std::vector<int> PathBuilder::astar(int startId, int goalId) const {
    const int n = static_cast<int>(g_.size());
    if (startId < 0 || goalId < 0 || startId >= n || goalId >= n) {
        return {};
    }

    // Концом дороги не может быть клетка с деревом: путь не строим вовсе.
    // Старт может быть занят (Explorer уже стоит там) — выехать с него можно.
    if (startId != goalId && isBlocked(goalId)) {
        return {};
    }

    auto heuristic = [&](int a, int b) {
        if (topology_) {
            const auto& ca = topology_->cells[static_cast<size_t>(a)];
            const auto& cb = topology_->cells[static_cast<size_t>(b)];
            const float dot = std::clamp(QVector3D::dotProduct(
                ca.centroid.normalized(), cb.centroid.normalized()), -1.0f, 1.0f);
            return std::acos(dot);
        }
        return edgeAngularDistance(model_->cells()[static_cast<size_t>(a)],
            model_->cells()[static_cast<size_t>(b)]);
        };

    constexpr float INF = std::numeric_limits<float>::infinity();
    std::vector<float> g(n, INF);
    std::vector<float> f(n, INF);
    std::vector<int> parent(n, -1);
    std::vector<char> closed(n, 0);

    struct Node {
        float f;
        int id;
    };
    struct Compare {
        bool operator()(const Node& a, const Node& b) const { return a.f > b.f; }
    };

    std::priority_queue<Node, std::vector<Node>, Compare> pq;
    g[startId] = 0.0f;
    f[startId] = heuristic(startId, goalId);
    pq.push({ f[startId], startId });

    while (!pq.empty()) {
        const int u = pq.top().id;
        pq.pop();

        if (closed[u]) {
            continue;
        }
        closed[u] = 1;
        if (u == goalId) {
            break;
        }

        for (const auto& edge : g_[u]) {
            const int v = edge.to;
            if (closed[v]) {
                continue;
            }

            const float candidate = g[u] + edge.w;
            if (candidate < g[v]) {
                g[v] = candidate;
                f[v] = candidate + heuristic(v, goalId);
                parent[v] = u;
                pq.push({ f[v], v });
            }
        }
    }

    if (parent[goalId] == -1 && startId != goalId) {
        return {};
    }

    std::vector<int> path;
    for (int cur = goalId; cur != -1; cur = parent[cur]) {
        path.push_back(cur);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<QVector3D> PathBuilder::polylineOnSphere(const std::vector<int>& path,
    int segmentsPerEdge,
    float bias,
    float heightStep) const {
    std::vector<QVector3D> out;
    if (path.size() < 2) {
        return out;
    }

    segmentsPerEdge = std::max(2, segmentsPerEdge);

    auto slerp = [](const QVector3D& a, const QVector3D& b, float t) {
        const QVector3D an = a.normalized();
        const QVector3D bn = b.normalized();
        const float dot = std::clamp(QVector3D::dotProduct(an, bn), -1.0f, 1.0f);
        const float angle = std::acos(dot);
        if (angle < 1e-6f) {
            return an;
        }
        const float sinAngle = std::sin(angle);
        return (std::sin((1.0f - t) * angle) / sinAngle) * an +
            (std::sin(t * angle) / sinAngle) * bn;
        };

    if (topology_ && state_) {
        std::vector<QVector3D> out;
        out.reserve((path.size() - 1) * static_cast<size_t>(segmentsPerEdge) + 1);
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            const auto& from = topology_->cells[static_cast<size_t>(path[i])];
            const auto& to = topology_->cells[static_cast<size_t>(path[i + 1])];
            for (int k = 0; k < segmentsPerEdge; ++k) {
                const float t = float(k) / float(segmentsPerEdge);
                const float h = (1.0f - t) * float((*state_)[static_cast<size_t>(path[i])].height)
                    + t * float((*state_)[static_cast<size_t>(path[i + 1])].height);
                out.push_back(slerp(from.centroid, to.centroid, t).normalized()
                    * (1.0f + h * heightStep + bias));
            }
        }
        if (!path.empty()) {
            const auto& last = topology_->cells[static_cast<size_t>(path.back())];
            const float h = float((*state_)[static_cast<size_t>(path.back())].height);
            out.push_back(last.centroid.normalized() * (1.0f + h * heightStep + bias));
        }
        return out;
    }

    const auto& cells = model_->cells();
    out.reserve((path.size() - 1) * static_cast<size_t>(segmentsPerEdge) + 1);

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Cell& from = cells[static_cast<size_t>(path[i])];
        const Cell& to = cells[static_cast<size_t>(path[i + 1])];

        for (int k = 0; k < segmentsPerEdge; ++k) {
            const float t = float(k) / float(segmentsPerEdge);
            const float h = (1.0f - t) * float(from.height) + t * float(to.height);
            const float radius = 1.0f + h * heightStep + bias;
            out.push_back(slerp(from.centroid, to.centroid, t).normalized() * radius);
        }
    }

    const Cell& last = cells[static_cast<size_t>(path.back())];
    out.push_back(last.centroid.normalized() * (1.0f + last.height * heightStep + bias));
    return out;
}

