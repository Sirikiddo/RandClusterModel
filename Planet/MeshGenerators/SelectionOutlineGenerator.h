#pragma once

#include <QSet>
#include <vector>

#include "HexSphereModel.h"

class SelectionOutlineGenerator {
public:
    static std::vector<float> buildSelectionOutlineVertices(
        const HexSphereModel& model,
        const QSet<int>& selectedCells,
        float heightStep,
        float outlineBias,
        bool smoothOneStep);
};
