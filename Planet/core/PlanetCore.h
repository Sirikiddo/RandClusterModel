#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_set>
#include <vector>

#include "UiCommands.h"
#include "WorkOrder.h"

class PlanetCore {
public:
    struct LightWork {
        // Contract: applied synchronously each tick before heavy WorkOrder.
        // Must remain lightweight: selection-only updates, no rebuild/regenerate/uploadScene.
        bool clearSelection = false;
        std::vector<int> toggleCells{};

        // Coalescing for repeated toggles in one frame: keep latest order and uniqueness in O(1) average.
        std::unordered_set<int> toggleCellsSet{};

        void queueToggleCell(int cellId);
        bool hasWork() const;
    };

    void enqueue(UiCommand command);
    void applyQueuedInputs();

    std::optional<LightWork> peekLight() const;
    void consumeLight();

    std::optional<WorkOrder> peekWork() const;
    void consumeWork();

    // Boundary #1 semantic: sceneVersion increments per accepted user intent command.
    // Keep command stream meaningful: avoid high-frequency per-pixel intents.
    uint64_t sceneVersion() const { return sceneVersion_; }

private:
    void applyCommand(const UiCommand& command);

    std::deque<UiCommand> queuedInputs_{};
    LightWork light_{};
    WorkOrder work_{};
    uint64_t sceneVersion_ = 0;
};
