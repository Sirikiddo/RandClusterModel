#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "renderers/TerrainTessellator.h"

class TerrainDagPayloadStore {
public:
    std::string putTerrainMesh(TerrainMesh mesh) {
        return put("terrainMesh", std::move(mesh));
    }

    std::string putVisibleIndices(std::vector<uint32_t> indices) {
        return put("visibleTerrainIndices", std::move(indices));
    }

    const TerrainMesh* findTerrainMesh(const std::string& token) const {
        const auto it = payloads_.find(token);
        if (it == payloads_.end()) {
            return nullptr;
        }
        return std::get_if<TerrainMesh>(&it->second);
    }

    const std::vector<uint32_t>* findVisibleIndices(const std::string& token) const {
        const auto it = payloads_.find(token);
        if (it == payloads_.end()) {
            return nullptr;
        }
        return std::get_if<std::vector<uint32_t>>(&it->second);
    }

private:
    template <class T>
    std::string put(const char* prefix, T payload) {
        const std::string token = std::string(prefix) + ":" + std::to_string(nextId_++);
        payloads_[token] = std::move(payload);
        return token;
    }

    using Payload = std::variant<TerrainMesh, std::vector<uint32_t>>;

    uint64_t nextId_ = 1;
    std::unordered_map<std::string, Payload> payloads_;
};
