#include "model/HexSphereTopology.h"

#include <mutex>
#include <deque>

#include "model/HexSphereModel.h"

namespace {
// Keep a small bounded LRU.  A level switch should not force reconstruction
// when the user returns to a recently used level.
std::mutex g_topologyCacheMutex;
std::deque<std::pair<int, HexSphereTopologyPtr>> g_topologyCache;
constexpr size_t kTopologyCacheCapacity = 3;

HexSphereTopologyPtr buildTopologyUncached(int subdivisionLevel) {
    HexSphereModel builderModel;
    IcosphereBuilder builder;
    builderModel.rebuildFromIcosphere(builder.build(subdivisionLevel), subdivisionLevel);
    // rebuildFromIcosphere already materializes the immutable topology.
    return builderModel.topology();
}
} // namespace

HexSphereTopologyPtr buildHexSphereTopology(int subdivisionLevel) {
    std::lock_guard<std::mutex> lock(g_topologyCacheMutex);
    for (auto it = g_topologyCache.begin(); it != g_topologyCache.end(); ++it) {
        if (it->first == subdivisionLevel) {
            auto result = *it;
            g_topologyCache.erase(it);
            g_topologyCache.push_back(result);
            return result.second;
        }
    }
    auto result = buildTopologyUncached(subdivisionLevel);
    g_topologyCache.emplace_back(subdivisionLevel, result);
    if (g_topologyCache.size() > kTopologyCacheCapacity) {
        g_topologyCache.pop_front();
    }
    return result;
}
