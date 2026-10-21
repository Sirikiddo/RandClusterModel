#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include "TerrainBackendTypes.h"

// Temporary adapter for a synchronous, single-process backend. ProcessDAG still
// transports short strings; its consumer resolves a token to shared_ptr<const T>.
// This side channel is not a general transport contract. Typed engine payloads
// are needed before adding independent consumers/processes or asynchronous runs.
// Tokens never contain addresses, are never reused, and belong to this store.
// Shared ownership keeps resolved values alive after eviction. Retention is
// bounded by entry count (not bytes); this is not an L10 memory budget.
template<class T>
class InProcessDagPayloads {
public:
    using Ptr = std::shared_ptr<const T>;
    explicit InProcessDagPayloads(std::string prefix) : prefix_(std::move(prefix)) {}

    template<class Equal>
    std::string intern(const T& value, Equal equal) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (equal(*it->second, value)) {
                auto entry = *it;
                entries_.erase(it);
                entries_.push_back(entry);
                return entry.first;
            }
        }
        return put(std::make_shared<const T>(value));
    }

    std::string put(T value) { return put(std::make_shared<const T>(std::move(value))); }
    std::string put(Ptr value) {
        if (!value) throw std::invalid_argument("Cannot publish a null DAG payload");
        const std::string token = prefix_ + ':' + std::to_string(++revision_);
        entries_.emplace_back(token, std::move(value));
        if (entries_.size() > 8) entries_.pop_front();
        return token;
    }

    Ptr resolve(std::string_view token) const {
        for (const auto& [key, value] : entries_) if (key == token) return value;
        throw std::runtime_error("Missing or expired in-process DAG payload: " + std::string(token));
    }

private:
    std::string prefix_;
    uint64_t revision_ = 0;
    std::deque<std::pair<std::string, Ptr>> entries_;
};

template<class T>
class InProcessDagCache {
public:
    using Ptr = std::shared_ptr<const T>;
    template<class Build>
    std::pair<Ptr, bool> get(const std::string& key, Build build) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->first == key) {
                auto entry = *it;
                entries_.erase(it);
                entries_.push_back(entry);
                return {entry.second, true};
            }
        }
        auto value = std::make_shared<const T>(build());
        entries_.emplace_back(key, value);
        if (entries_.size() > 8) entries_.pop_front();
        return {value, false};
    }
private:
    std::deque<std::pair<std::string, Ptr>> entries_;
};

inline bool sameTerrainSnapshot(const TerrainSnapshot& a, const TerrainSnapshot& b) {
    if (a.subdivisionLevel != b.subdivisionLevel || a.generatorIndex != b.generatorIndex
        || a.params.seed != b.params.seed || a.params.scale != b.params.scale
        || a.params.seaLevel != b.params.seaLevel || a.cells.size() != b.cells.size()) return false;
    for (size_t i = 0; i < a.cells.size(); ++i) {
        const auto& x = a.cells[i]; const auto& y = b.cells[i];
        if (x.height != y.height || x.biome != y.biome || x.temperature != y.temperature
            || x.humidity != y.humidity || x.pressure != y.pressure
            || x.oreDensity != y.oreDensity || x.oreType != y.oreType) return false;
    }
    return true;
}
