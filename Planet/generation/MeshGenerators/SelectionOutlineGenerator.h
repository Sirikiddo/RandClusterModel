#pragma once

#include <QSet>
#include <QVector3D>
#include <vector>

#include "model/HexSphereModel.h"

struct SelectionOutlineEdge {
    QVector3D startUnit;
    QVector3D endUnit;
    int cellHeight = 0;
    int neighborHeight = 0;
    bool hasNeighbor = false;
};

struct SelectionOutlineInput {
    std::vector<SelectionOutlineEdge> edges;
    float heightStep = 0.0f;
    float outlineBias = 0.0f;
    bool smoothOneStep = false;
};

class SelectionOutlineGenerator {
public:
    static SelectionOutlineInput buildSelectionOutlineInput(
        const HexSphereModel& model,
        const QSet<int>& selectedCells,
        float heightStep,
        float outlineBias,
        bool smoothOneStep);

    static std::vector<float> buildSelectionOutlineVertices(const SelectionOutlineInput& input);

    static std::vector<float> buildSelectionOutlineVertices(
        const HexSphereModel& model,
        const QSet<int>& selectedCells,
        float heightStep,
        float outlineBias,
        bool smoothOneStep);
};
