#pragma once
#include <cstdint>
#include "DebugOverlay.h"

class InputController;

class EngineFacade {
public:
    explicit EngineFacade(InputController& legacy);

    void tick(float dtSeconds);

    bool shouldRender() const { return true; }

    const DebugOverlay& overlay() const { return overlay_; }

    void bumpVersion() { ++overlay_.sceneVersion; }
    void setDirty(bool v) { overlay_.hasPlan = v; }
    void setAsyncBusy(bool v) { overlay_.asyncBusy = v; }

private:
    InputController& legacy_;
    DebugOverlay overlay_;

    float fpsAccum_ = 0.0f;
    int   fpsFrames_ = 0;
};
