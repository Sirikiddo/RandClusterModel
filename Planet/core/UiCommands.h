#pragma once

#include <variant>

#include "generation/TerrainGenerator.h"

struct CmdSetSubdivisionLevel {
    int level = 0;
};

struct CmdRegenerateTerrain {};

struct CmdToggleCell {
    int cellId = -1;
};

struct CmdClearSelection {};

struct CmdSetGenerator {
    int index = 0;
};

struct CmdSetParams {
    TerrainParams params{};
};

struct CmdSetSmoothOneStep {
    bool enabled = false;
};

struct CmdSetStripInset {
    float value = 0.0f;
};

struct CmdSetOutlineBias {
    float value = 0.0f;
};

using UiCommand = std::variant<
    CmdSetSubdivisionLevel,
    CmdRegenerateTerrain,
    CmdToggleCell,
    CmdClearSelection,
    CmdSetGenerator,
    CmdSetParams,
    CmdSetSmoothOneStep,
    CmdSetStripInset,
    CmdSetOutlineBias>;
