#include "EngineFacade.h"
#include "controllers/InputController.h"

EngineFacade::EngineFacade(InputController& legacy)
    : legacy_(legacy) {
}

void EngineFacade::tick(float dtSeconds) {
    // dt -> overlay
    overlay_.dtMs = dtSeconds * 1000.0f;

    fpsAccum_ += dtSeconds;
    ++fpsFrames_;
    if (fpsAccum_ >= 0.5f) {
        overlay_.fps = fpsFrames_ / fpsAccum_;
        fpsAccum_ = 0.0f;
        fpsFrames_ = 0;
    }

    (void)legacy_;
}
