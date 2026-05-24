#pragma once

#include <memory>
#include <vector>

#include "TerrainBackendTypes.h"  // для TerrainSnapshot

// Структура результата поиска пути
struct PathResult {
    bool found = false;
    float length = 0.0f;
    std::vector<int> cellIds;  // ID ячеек пути
};

class DagPathBackend {
public:
    static constexpr bool usesDagPath = true;

    DagPathBackend();
    ~DagPathBackend();

    // Move-семантика
    DagPathBackend(DagPathBackend&&) noexcept;
    DagPathBackend& operator=(DagPathBackend&&) noexcept;

    // Copy запрещён
    DagPathBackend(const DagPathBackend&) = delete;
    DagPathBackend& operator=(const DagPathBackend&) = delete;

    // ===== Публичный интерфейс =====

    // Установка снапшота террейна (из TerrainBackend)
    void setTerrainSnapshot(const TerrainSnapshot& snapshot);

    // Параметр сглаживания подъёмов
    void setSmoothMaxDelta(int delta);

    // Поиск пути между двумя ячейками
    PathResult findPath(int startCellId, int goalCellId);

    // Получить последний результат (без перевычисления)
    const PathResult& lastResult() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};