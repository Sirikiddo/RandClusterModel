#include "PathBuilder.h"

void PathBuilder::build(PathBuilder::WeightFn w) const {
    const auto& cells = model_.cells();
    const int n = (int)cells.size();
    g_.assign(n, {});
    for (int u = 0; u < n; ++u) {
        const auto& cu = cells[(size_t)u];
        for (int v : cu.neighbors) if (v >= 0) {
            float ww = w ? w(cu, cells[(size_t)v]) : 1.0f;
            g_[u].push_back({ v, ww });
        }
    }
}

std::vector<int> PathBuilder::astar(int startId, int goalId) const {
    const int n = (int)g_.size();
    if (startId < 0 || goalId < 0 || startId >= n || goalId >= n) return {};

    auto gc = [&](int id) { return model_.cells()[(size_t)id].centroid.normalized(); };
    auto h = [&](int a, int b) {
        float d = std::clamp(QVector3D::dotProduct(gc(a), gc(b)), -1.0f, 1.0f);
        return std::acos(d); // great-circle (радианы)
        };

    constexpr float INF = std::numeric_limits<float>::infinity();
    std::vector<float> g(n, INF), f(n, INF);
    std::vector<int>   parent(n, -1);
    std::vector<char>  closed(n, 0);

    struct Node { float f; int id; };
    struct Cmp { bool operator()(const Node& a, const Node& b)const { return a.f > b.f; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> pq;

    g[startId] = 0.0f; f[startId] = h(startId, goalId);
    pq.push({ f[startId], startId });

    while (!pq.empty()) {
        int u = pq.top().id; pq.pop();
        if (closed[u]) continue;
        closed[u] = 1;
        if (u == goalId) break;

        for (auto [v, w] : g_[u]) {
            if (closed[v]) continue;
            float cand = g[u] + w;
            if (cand < g[v]) {
                g[v] = cand;
                f[v] = cand + h(v, goalId);
                parent[v] = u;
                pq.push({ f[v], v });
            }
        }
    }

    if (parent[goalId] == -1 && startId != goalId) return {};
    std::vector<int> path;
    for (int cur = goalId; cur != -1; cur = parent[cur]) path.push_back(cur);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<QVector3D> PathBuilder::polylineOnSphere(const std::vector<int>& path,
    int segmentsPerEdge,
    float bias,
    float heightStep) const
{
    std::vector<QVector3D> out;
    if (path.size() < 2) return out;
    segmentsPerEdge = std::max(2, segmentsPerEdge);

    auto slerp = [](const QVector3D& a, const QVector3D& b, float t) {
        QVector3D an = a.normalized(), bn = b.normalized();
        float dot = std::clamp(QVector3D::dotProduct(an, bn), -1.0f, 1.0f);
        float ang = std::acos(dot);
        if (ang < 1e-6f) return an;
        float s = std::sin(ang);
        return (std::sin((1.0f - t) * ang) / s) * an + (std::sin(t * ang) / s) * bn;
        };

    const auto& cells = model_.cells();
    out.reserve((path.size() - 1) * (size_t)segmentsPerEdge);

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const int a = path[i], b = path[i + 1];
        const auto& ca = cells[(size_t)a];
        const auto& cb = cells[(size_t)b];
        const QVector3D A = ca.centroid, B = cb.centroid;
        for (int k = 0; k < segmentsPerEdge; ++k) {
            float t = float(k) / float(segmentsPerEdge);
            float h = (1.0f - t) * float(ca.height) + t * float(cb.height);
            float R = 1.0f + h * heightStep + bias;
            out.push_back(slerp(A, B, t).normalized() * R);
        }
    }
    // добавить конечную точку точно
    {
        const auto& cEnd = model_.cells()[(size_t)path.back()];
        float R = 1.0f + cEnd.height * heightStep + bias;
        out.push_back(cEnd.centroid.normalized() * R);
    }
    return out;
}