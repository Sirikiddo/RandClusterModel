#include "WireMeshGenerator.h"

std::vector<float> WireMeshGenerator::buildWireVertices(const HexSphereModel& model) {
    const auto& vertices = model.dualVerts();
    std::vector<float> lineVerts;
    lineVerts.reserve(model.wireEdges().size() * 6);

    for (auto [a, b] : model.wireEdges()) {
        const auto& pa = vertices[size_t(a)];
        const auto& pb = vertices[size_t(b)];
        lineVerts.insert(lineVerts.end(), { pa.x(), pa.y(), pa.z(), pb.x(), pb.y(), pb.z() });
    }

    return lineVerts;
}
