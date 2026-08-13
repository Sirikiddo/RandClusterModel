#pragma once

#include <memory>
#include <vector>

#include "TerrainBackendTypes.h"

struct PathResult {
    bool found = false;
    float length = 0.0f;
    std::vector<int> cellIds;
};

class DagPathBackend {
public:
    static constexpr bool usesDagPath = true;

    DagPathBackend();
    ~DagPathBackend();

    DagPathBackend(DagPathBackend&&) noexcept;
    DagPathBackend& operator=(DagPathBackend&&) noexcept;

    DagPathBackend(const DagPathBackend&) = delete;
    DagPathBackend& operator=(const DagPathBackend&) = delete;

    void setTerrainSnapshot(const TerrainSnapshot& snapshot);
    void setSmoothMaxDelta(int delta);
    PathResult findPath(int startCellId, int goalCellId);
    const PathResult& lastResult() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
