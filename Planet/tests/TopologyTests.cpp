#include "tests/TopologyTests.h"

#include <stdexcept>

#include "model/HexSphereModel.h"
#include "model/HexSphereTopology.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testRepeatedBuildUsesBoundedCache() {
    HexSphereModel::resetTopologyBuildCount();
    const auto first = buildHexSphereTopology(2);
    require(first != nullptr, "topology must be built");
    require(HexSphereModel::topologyBuildCount() == 1,
        "first build must construct topology exactly once");

    const auto second = buildHexSphereTopology(2);
    require(first == second, "same level must reuse the cached topology");
    require(HexSphereModel::topologyBuildCount() == 1,
        "cache hit must not rebuild topology");

    const auto otherLevel = buildHexSphereTopology(1);
    require(otherLevel != nullptr && otherLevel != first,
        "level switch must build a different topology");
    require(HexSphereModel::topologyBuildCount() == 2,
        "level switch must build topology exactly once");

    const auto back = buildHexSphereTopology(2);
    require(back == first,
        "recently used level must remain in the bounded cache");
    require(HexSphereModel::topologyBuildCount() == 2,
        "returning to a cached level must not rebuild it");
}

void testModelFromTopologyDoesNotRebuild() {
    const auto topology = buildHexSphereTopology(2);
    HexSphereModel::resetTopologyBuildCount();

    HexSphereModel model(topology);
    require(HexSphereModel::topologyBuildCount() == 0,
        "wrapping shared topology must not rebuild it");
    require(model.cellCount() == topology->cellCount(),
        "model cell count must match topology");
    require(!model.pickTris().empty(), "pick fans must not be empty");
    require(model.pickTris().size() == topology->pickTris.size(),
        "pick fans must be reused from the shared topology");
    require(model.cells().front().poly.sharedData() == topology->cells.front().poly.sharedData(),
        "cell polygon indices must share topology storage");
    require(model.cells().front().neighbors.sharedData() == topology->cells.front().neighbors.sharedData(),
        "cell adjacency must share topology storage");

    const size_t cellsBefore = model.cells().size();
    model.resetCellStates();
    require(model.cells().size() == cellsBefore,
        "resetting state must keep the topology cells");
    require(HexSphereModel::topologyBuildCount() == 0,
        "resetting state must not rebuild topology");

    model.rebuildPickTris();
    require(HexSphereModel::topologyBuildCount() == 0,
        "pick rebuild must not rebuild topology");
}

} // namespace

void runTopologyUnitTests() {
    testRepeatedBuildUsesBoundedCache();
    testModelFromTopologyDoesNotRebuild();
}
