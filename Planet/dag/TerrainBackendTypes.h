#pragma once

#include <string>
#include <vector>

#include "generation/TerrainGenerator.h"
#include "model/HexSphereModel.h"

struct TerrainCellSnapshot {
    int height = 0;
    Biome biome = Biome::Grass;
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;
    float oreDensity = 0.0f;
    OreType oreType = OreType::None;
};

struct TerrainSnapshot {
    int subdivisionLevel = kDefaultTerrainSubdivisionLevel;
    int generatorIndex = kDefaultTerrainGeneratorIndex;
    TerrainParams params = defaultTerrainParams();
    // In-process immutable topology handle. It is intentionally absent from
    // serialized JSON; deserialized snapshots must be attached to a topology
    // before they can enter Scene or Path DAG.
    HexSphereTopologyPtr topology;
    std::vector<TerrainCellSnapshot> cells;

    // Клетки, занятые деревьями (отсортированные, без повторов). Дерево — препятствие
    // для дороги: через такую клетку нельзя проложить путь и нельзя сделать её концом
    // дороги. Набор заполняет HexSphereSceneController::captureTerrainSnapshot() из
    // фактического размещения деревьев, поэтому path backend всегда блокирует ровно
    // те клетки, на которых стоит дерево на экране.
    std::vector<int> treeCells;

    // Клетки, занятые руслом реки (отсортированные, без повторов). Река — такое
    // же препятствие для дороги, как дерево: машина не может проехать через
    // такую клетку и не может на ней остановиться. Набор заполняет
    // HexSphereSceneController::captureTerrainSnapshot() из сгенерированных рек.
    std::vector<int> riverCells;

    bool empty() const noexcept {
        return cells.empty();
    }
};

struct TerrainRegenerationResult {
    bool ok = true;
    std::string message;

    explicit operator bool() const noexcept {
        return ok;
    }

    static TerrainRegenerationResult success() {
        return {};
    }

    static TerrainRegenerationResult failure(std::string text) {
        TerrainRegenerationResult result;
        result.ok = false;
        result.message = std::move(text);
        return result;
    }
};
