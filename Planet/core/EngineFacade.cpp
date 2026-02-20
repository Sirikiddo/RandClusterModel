#include "EngineFacade.h"

#include <utility>

#include "controllers/InputController.h"

EngineFacade::EngineFacade(InputController& legacy)
    : legacy_(legacy) {
}

void EngineFacade::tick(float dtSeconds) {
    overlay_.dtMs = dtSeconds * 1000.0f;

    fpsAccum_ += dtSeconds;
    ++fpsFrames_;
    if (fpsAccum_ >= 0.5f) {
        overlay_.fps = fpsFrames_ / fpsAccum_;
        fpsAccum_ = 0.0f;
        fpsFrames_ = 0;
    }

    core_.applyQueuedInputs();
    overlay_.sceneVersion = core_.sceneVersion();

    if (const auto work = core_.peekWork()) {
        overlay_.hasPlan = true;
        executeWorkOrder(*work);
        core_.consumeWork();
    }

    overlay_.hasPlan = core_.peekWork().has_value();
}

void EngineFacade::handleUiCommand(UiCommand command) {
    core_.enqueue(std::move(command));
}

void EngineFacade::executeWorkOrder(const WorkOrder& work) {
    if (work.newLevel) {
        legacy_.setSubdivisionLevel(*work.newLevel);
    }

    if (work.generatorIndex) {
        legacy_.setGeneratorByIndex(*work.generatorIndex);
    }

    if (work.terrainParams) {
        legacy_.setTerrainParams(*work.terrainParams);
    }

    if (work.smoothOneStep) {
        legacy_.setSmoothOneStep(*work.smoothOneStep);
    }

    if (work.stripInset) {
        legacy_.setStripInset(*work.stripInset);
    }

    if (work.outlineBias) {
        legacy_.setOutlineBias(*work.outlineBias);
    }

    for (const int cellId : work.toggleCells) {
        (void)cellId;
    }

    if (work.regenerateTerrain) {
        legacy_.regenerateTerrain();
    }
}
