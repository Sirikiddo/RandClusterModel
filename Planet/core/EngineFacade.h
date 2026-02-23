#pragma once
#include <cstdint>
#include "DebugOverlay.h"

class InputController;

class EngineFacade {
public:
    explicit EngineFacade(InputController& legacy);

    // sync tick: пока без логики, но считает fps/состояние
    void tick(float dtSeconds);

    // в коммите 1 рендер решает UI как и раньше, но пусть будет
    bool shouldRender() const { return true; }

    const DebugOverlay& overlay() const { return overlay_; }

    // на будущее (со 2-го коммита)
    void bumpVersion() { ++overlay_.sceneVersion; }
    void setDirty(bool v) { overlay_.hasPlan = v; }
    void setAsyncBusy(bool v) { overlay_.asyncBusy = v; }

private:
    InputController& legacy_;
    DebugOverlay overlay_;

    // простейший FPS фильтр
    float fpsAccum_ = 0.0f;
    int   fpsFrames_ = 0;
};
