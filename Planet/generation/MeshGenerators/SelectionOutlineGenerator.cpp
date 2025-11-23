#include "SelectionOutlineGenerator.h"

std::vector<float> SelectionOutlineGenerator::buildSelectionOutlineVertices(
    const HexSphereModel& model,
    const QSet<int>& selectedCells,
    float heightStep,
    float outlineBias,
    bool smoothOneStep) {
    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();
    constexpr float R = 1.0f;

    std::vector<float> lineVerts;
    lineVerts.reserve(selectedCells.size() * 12);

    auto liftBias = [&](const QVector3D& u, float h) { return u.normalized() * (R + h * heightStep + outlineBias); };

    for (int cid : selectedCells) {
        const auto& c = cells[size_t(cid)];
        const int deg = int(c.poly.size());
        for (int i = 0; i < deg; ++i) {
            const int j = (i + 1) % deg;
            const int va = c.poly[i];
            const int vb = c.poly[j];
            float hA = float(c.height), hB = float(c.height);
            const int nEdge = c.neighbors[i];
            if (nEdge >= 0) {
                const int hN = cells[size_t(nEdge)].height;
                const int d = std::abs(hN - c.height);
                if (smoothOneStep && d == 1) {
                    const float mid = 0.5f * float(hN + c.height);
                    hA = hB = mid;
                }
            }
            const QVector3D pA = liftBias(dual[size_t(va)], hA);
            const QVector3D pB = liftBias(dual[size_t(vb)], hB);
            lineVerts.insert(lineVerts.end(), { pA.x(), pA.y(), pA.z(), pB.x(), pB.y(), pB.z() });
        }
    }

    return lineVerts;
}
