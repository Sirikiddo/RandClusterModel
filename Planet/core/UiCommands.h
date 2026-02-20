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

<<<<<<< codex/add-planetcore-and-command-queue-7ojbbx
=======
struct CmdClearSelection {};

>>>>>>> main
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
<<<<<<< codex/add-planetcore-and-command-queue-7ojbbx
=======
    CmdClearSelection,
>>>>>>> main
    CmdSetGenerator,
    CmdSetParams,
    CmdSetSmoothOneStep,
    CmdSetStripInset,
    CmdSetOutlineBias>;
