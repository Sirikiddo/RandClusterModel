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

void PathBuilder::build(PathBuilder::WeightFn w) const {
    const auto& cells = model_.cells();
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

bool PathBuilder::isTraversable(const Cell& from, const Cell& to) {
    return (to.height - from.height) <= kMaxClimbDelta;
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

float PathBuilder::traversalCost(const Cell& from, const Cell& to) {
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

    auto heuristic = [&](int a, int b) {
        return edgeAngularDistance(model_.cells()[static_cast<size_t>(a)],
            model_.cells()[static_cast<size_t>(b)]);
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

    const auto& cells = model_.cells();
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