#include "EngineFacade.h"

#include <utility>

#include "controllers/InputController.h"
#include "DebugMacros.h"

EngineFacade::EngineFacade(InputController& legacy)
    : legacy_(legacy) {
}

void EngineFacade::tick(float dtSeconds) {
    DEBUG_CALL_PARAM("dt=" << dtSeconds);
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
        DEBUG_CALL_PARAM("has work: true");
        overlay_.hasPlan = true;
        executeWorkOrder(*work);
        core_.consumeWork();
    }
    else
    {
        DEBUG_CALL_PARAM("has work: false");
    }

    overlay_.hasPlan = core_.peekWork().has_value();
}

void EngineFacade::handleUiCommand(UiCommand command) {
    DEBUG_CALL();
    core_.enqueue(std::move(command));
}

void EngineFacade::executeWorkOrder(const WorkOrder& work) {
    DEBUG_CALL();
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
        DEBUG_CALL_PARAM("stripInset=" << *work.stripInset);
        legacy_.setStripInset(*work.stripInset);
    }

    if (work.outlineBias) {
        legacy_.setOutlineBias(*work.outlineBias);
    }

    for (const int cellId : work.toggleCells) {
        legacy_.toggleCellSelection(cellId);
    }

    // 2. ПОСЛЕ применения всех параметров - ОДИН раз перестраиваем модель
    if (work.needsRebuild || work.regenerateTerrain) {
        if (work.regenerateTerrain) {
            legacy_.regenerateTerrain();  // это вызовет rebuildModel внутри
        }
        else {
            legacy_.rebuildModel();  // <-- НУЖНО ДОБАВИТЬ ЭТОТ МЕТОД В InputController
        }
    }
}
