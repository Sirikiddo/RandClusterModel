#pragma once

#include <functional>
#include <limits>
#include <queue>
#include <vector>

#include <QVector3D>

#include "model/HexSphereModel.h"

class PathBuilder {
public:
    using WeightFn = std::function<float(const Cell&, const Cell&)>;
    static constexpr int kMaxClimbDelta = 2;

    explicit PathBuilder(const HexSphereModel& model) : model_(model) {}

    void build(WeightFn w = nullptr) const;
    std::vector<int> astar(int startId, int goalId) const;

    std::vector<QVector3D> polylineOnSphere(const std::vector<int>& path,
        int segmentsPerEdge,
        float bias,
        float heightStep) const;

    static bool isTraversable(const Cell& from, const Cell& to);
    static float edgeAngularDistance(const Cell& from, const Cell& to);
    static float biomeTraversalFactor(Biome biome);
    static float traversalCost(const Cell& from, const Cell& to);

private:
    struct Adj {
        int to;
        float w;
    };

    const HexSphereModel& model_;
    mutable std::vector<std::vector<Adj>> g_;
};