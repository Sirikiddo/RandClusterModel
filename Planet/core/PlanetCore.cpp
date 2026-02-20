#include "PlanetCore.h"

#include <algorithm>
#include <type_traits>
#include <utility>

void PlanetCore::LightWork::queueToggleCell(int cellId) {
    if (toggleCellsSet.find(cellId) != toggleCellsSet.end()) {
        // last-wins for duplicates in one batch: move repeated cell to the back.
        toggleCells.erase(std::remove(toggleCells.begin(), toggleCells.end(), cellId), toggleCells.end());
    }

    toggleCellsSet.insert(cellId);
    toggleCells.push_back(cellId);
}

bool PlanetCore::LightWork::hasWork() const {
    return clearSelection || !toggleCells.empty();
}

void PlanetCore::enqueue(UiCommand command) {
    queuedInputs_.push_back(std::move(command));
}

void PlanetCore::applyQueuedInputs() {
    while (!queuedInputs_.empty()) {
        const UiCommand command = std::move(queuedInputs_.front());
        queuedInputs_.pop_front();

        // sceneVersion semantic: every accepted user intent command increments version.
        // Do not enqueue per-pixel/noisy commands (e.g. mouse move streams).
        applyCommand(command);
        ++sceneVersion_;
    }
}

std::optional<PlanetCore::LightWork> PlanetCore::peekLight() const {
    if (!light_.hasWork()) {
        return std::nullopt;
    }
    return light_;
}

void PlanetCore::consumeLight() {
    light_ = LightWork{};
}

std::optional<WorkOrder> PlanetCore::peekWork() const {
    if (!work_.hasWork()) {
        return std::nullopt;
    }
    return work_;
}

void PlanetCore::consumeWork() {
    work_ = WorkOrder{};
}

void PlanetCore::applyCommand(const UiCommand& command) {
    std::visit([this](const auto& cmd) {
        using T = std::decay_t<decltype(cmd)>;

        if constexpr (std::is_same_v<T, CmdSetSubdivisionLevel>) {
            work_.newLevel = cmd.level;
            work_.rebuildMesh = true;
            work_.uploadBuffers = true;
        }
        else if constexpr (std::is_same_v<T, CmdRegenerateTerrain>) {
            work_.regenerateTerrain = true;
            work_.rebuildMesh = true;
            work_.uploadBuffers = true;
        }
        else if constexpr (std::is_same_v<T, CmdToggleCell>) {
            // Batch contract: duplicate toggles for same cell in one frame are coalesced.
            light_.queueToggleCell(cmd.cellId);
        }
        else if constexpr (std::is_same_v<T, CmdClearSelection>) {
            // Batch contract: clear dominates all previously queued toggles in this frame.
            // If clear is followed by toggles in same frame, toggles apply after clear.
            light_.clearSelection = true;
            light_.toggleCells.clear();
            light_.toggleCellsSet.clear();
        }
        else if constexpr (std::is_same_v<T, CmdSetGenerator>) {
            work_.generatorIndex = cmd.index;
        }
        else if constexpr (std::is_same_v<T, CmdSetParams>) {
            work_.terrainParams = cmd.params;
        }
        else if constexpr (std::is_same_v<T, CmdSetSmoothOneStep>) {
            work_.smoothOneStep = cmd.enabled;
            work_.rebuildMesh = true;
            work_.uploadBuffers = true;
        }
        else if constexpr (std::is_same_v<T, CmdSetStripInset>) {
            work_.stripInset = cmd.value;
            work_.rebuildMesh = true;
            work_.uploadBuffers = true;
        }
        else if constexpr (std::is_same_v<T, CmdSetOutlineBias>) {
            work_.outlineBias = cmd.value;
            work_.rebuildMesh = true;
            work_.uploadBuffers = true;
        }
        }, command);
}
