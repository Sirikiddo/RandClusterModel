#include "SelectionOutlineGenerator.h"

#include <algorithm>
#include <cmath>

SelectionOutlineInput SelectionOutlineGenerator::buildSelectionOutlineInput(
    const HexSphereModel& model,
    const QSet<int>& selectedCells,
    float heightStep,
    float outlineBias,
    bool smoothOneStep) {
    SelectionOutlineInput input;
    input.heightStep = heightStep;
    input.outlineBias = outlineBias;
    input.smoothOneStep = smoothOneStep;

    const auto& cells = model.cells();
    const auto& dual = model.dualVerts();
    std::vector<int> sortedIds(selectedCells.cbegin(), selectedCells.cend());
    std::sort(sortedIds.begin(), sortedIds.end());

    for (const int cellId : sortedIds) {
        if (cellId < 0 || size_t(cellId) >= cells.size()) {
            continue;
        }

        const auto& cell = cells[size_t(cellId)];
        const int edgeCount = int(cell.poly.size());
        for (int edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex) {
            const int nextIndex = (edgeIndex + 1) % edgeCount;
            const int startIndex = cell.poly[size_t(edgeIndex)];
            const int endIndex = cell.poly[size_t(nextIndex)];
            if (startIndex < 0 || endIndex < 0 ||
                size_t(startIndex) >= dual.size() || size_t(endIndex) >= dual.size()) {
                continue;
            }

            SelectionOutlineEdge edge;
            edge.startUnit = dual[size_t(startIndex)].normalized();
            edge.endUnit = dual[size_t(endIndex)].normalized();
            edge.cellHeight = cell.height;

            if (size_t(edgeIndex) < cell.neighbors.size()) {
                const int neighborId = cell.neighbors[size_t(edgeIndex)];
                if (neighborId >= 0 && size_t(neighborId) < cells.size()) {
                    edge.neighborHeight = cells[size_t(neighborId)].height;
                    edge.hasNeighbor = true;
                }
            }
            input.edges.push_back(edge);
        }
    }

    return input;
}

std::vector<float> SelectionOutlineGenerator::buildSelectionOutlineVertices(const SelectionOutlineInput& input) {
    constexpr float radius = 1.0f;
    std::vector<float> lineVertices;
    lineVertices.reserve(input.edges.size() * 6);

    for (const SelectionOutlineEdge& edge : input.edges) {
        float height = float(edge.cellHeight);
        if (input.smoothOneStep && edge.hasNeighbor &&
            std::abs(edge.neighborHeight - edge.cellHeight) == 1) {
            height = 0.5f * float(edge.neighborHeight + edge.cellHeight);
        }
        const float liftedRadius = radius + height * input.heightStep + input.outlineBias;
        const QVector3D start = edge.startUnit * liftedRadius;
        const QVector3D end = edge.endUnit * liftedRadius;
        lineVertices.insert(lineVertices.end(), {
            start.x(), start.y(), start.z(), end.x(), end.y(), end.z()
        });
    }
    return lineVertices;
}

std::vector<float> SelectionOutlineGenerator::buildSelectionOutlineVertices(
    const HexSphereModel& model,
    const QSet<int>& selectedCells,
    float heightStep,
    float outlineBias,
    bool smoothOneStep) {
    return buildSelectionOutlineVertices(buildSelectionOutlineInput(
        model, selectedCells, heightStep, outlineBias, smoothOneStep));
}
