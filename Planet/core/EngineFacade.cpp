#include "EngineFacade.h"
#include "controllers/InputController.h" // или твой путь

EngineFacade::EngineFacade(InputController& legacy)
    : legacy_(legacy) {
}

void EngineFacade::tick(float dtSeconds) {
    // dt -> overlay
    overlay_.dtMs = dtSeconds * 1000.0f;

    // fps сглаженный по окну ~0.5-1 сек
    fpsAccum_ += dtSeconds;
    ++fpsFrames_;
    if (fpsAccum_ >= 0.5f) {
        overlay_.fps = fpsFrames_ / fpsAccum_;
        fpsAccum_ = 0.0f;
        fpsFrames_ = 0;
    }

    // важный момент: никаких вызовов legacy тут пока нет
    (void)legacy_;
}
