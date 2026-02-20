#pragma once

#include "DebugOverlay.h"
#include "PlanetCore.h"

class InputController;

class EngineFacade {
public:
    explicit EngineFacade(InputController& legacy);

    void tick(float dtSeconds);
    void handleUiCommand(UiCommand command);

    bool shouldRender() const { return true; }

    const DebugOverlay& overlay() const { return overlay_; }

private:
    bool executeWorkOrder(const WorkOrder& work);

    InputController& legacy_;
    PlanetCore core_;
    DebugOverlay overlay_;

    float fpsAccum_ = 0.0f;
    int fpsFrames_ = 0;
};
