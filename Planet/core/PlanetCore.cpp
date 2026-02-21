#include "PlanetCore.h"

#include <type_traits>
#include <utility>

void PlanetCore::enqueue(UiCommand command) {
    queuedInputs_.push_back(std::move(command));
}

void PlanetCore::applyQueuedInputs() {
    while (!queuedInputs_.empty()) {
        UiCommand command = std::move(queuedInputs_.front());
        queuedInputs_.pop_front();

        applyCommand(command);
        ++sceneVersion_;
    }
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
        }
        else if constexpr (std::is_same_v<T, CmdRegenerateTerrain>) {
            work_.regenerateTerrain = true;
        }
        else if constexpr (std::is_same_v<T, CmdToggleCell>) {
            work_.toggleCells.push_back(cmd.cellId);
        }
        else if constexpr (std::is_same_v<T, CmdSetGenerator>) {
            work_.generatorIndex = cmd.index;
        }
        else if constexpr (std::is_same_v<T, CmdSetParams>) {
            work_.terrainParams = cmd.params;
        }
        else if constexpr (std::is_same_v<T, CmdSetSmoothOneStep>) {
            work_.smoothOneStep = cmd.enabled;
        }
        else if constexpr (std::is_same_v<T, CmdSetStripInset>) {
            work_.stripInset = cmd.value;
        }
        else if constexpr (std::is_same_v<T, CmdSetOutlineBias>) {
            work_.outlineBias = cmd.value;
        }
    }, command);
}
