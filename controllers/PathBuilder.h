#pragma once
#include <vector>
#include <queue>
#include <limits>
#include <functional>
#include <QVector3D>
#include "model/HexSphereModel.h"

class PathBuilder {
public:
    using WeightFn = std::function<float(const Cell&, const Cell&)>;

    explicit PathBuilder(const HexSphereModel& model) : model_(model) {}

    // соберём неориентированный граф по соседям (веса по умолчанию = 1)
    void build(WeightFn w = nullptr) const;

    // A* по центроидам ячеек (эвристика — дуговое расстояние по сфере)
    std::vector<int> astar(int startId, int goalId) const;

    // полилиния на сфере: дуги большой окружности между центроидами
    // segmentsPerEdge ≥ 2, радиус = 1 + lerp(ha,hb)*heightStep + bias
    std::vector<QVector3D> polylineOnSphere(const std::vector<int>& path,
        int segmentsPerEdge,
        float bias,
        float heightStep) const;

private:
    struct Adj { int to; float w; };
    const HexSphereModel& model_;
    mutable std::vector<std::vector<Adj>> g_;
};