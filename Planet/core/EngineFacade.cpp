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

    if (const auto light = core_.peekLight()) {
        // LightWork contract:
        // - executes synchronously before heavy work in the same tick
        // - uses legacy selection-outline uploads only (safe here: tick() runs from paintGL)
        if (light->clearSelection) {
            legacy_.clearSelection();
        }

        for (const int cellId : light->toggleCells) {
            legacy_.toggleCellSelection(cellId);
        }

        // Same as heavy path: consume only after successful synchronous apply.
        core_.consumeLight();
    }

    if (const auto work = core_.peekWork()) {
        // consumeWork() must happen only after successful synchronous execution.
        // If executeWorkOrder() throws, work remains pending and can be retried/diagnosed.
        const bool executed = executeWorkOrder(*work);
        if (executed) {
            core_.consumeWork();
        }
    }

    // Strictly mirrors PlanetCore pending heavy work (not async state).
    overlay_.hasPendingWork = core_.peekWork().has_value();
}

void EngineFacade::handleUiCommand(UiCommand command) {
    core_.enqueue(std::move(command));
}

bool EngineFacade::executeWorkOrder(const WorkOrder& work) {
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

    if (work.regenerateTerrain) {
        legacy_.regenerateTerrain();
    }

    return true;
}
