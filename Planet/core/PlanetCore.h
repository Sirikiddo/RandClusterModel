#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "UiCommands.h"
#include "WorkOrder.h"

class PlanetCore {
public:
    void enqueue(UiCommand command);
    void applyQueuedInputs();

    std::optional<WorkOrder> peekWork() const;
    void consumeWork();

    uint64_t sceneVersion() const { return sceneVersion_; }

private:
    void applyCommand(const UiCommand& command);

    std::deque<UiCommand> queuedInputs_{};
    WorkOrder work_{};
    uint64_t sceneVersion_ = 0;
};
