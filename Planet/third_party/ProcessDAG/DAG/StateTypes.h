#pragma once

#include "ProcTypes.h"
#include "StoreTypes.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace proc {

// Unified value model for all state layers.
using Value = std::string;
using ValueRef = std::shared_ptr<const Value>;
using ValueStore = std::unordered_map<Field, ValueRef>;

inline ValueRef make_value(std::string value) {
    return std::make_shared<const Value>(std::move(value));
}

// Current contract: identity-only comparison.
// We intentionally compare pointers (not payload) because values are treated as heavy objects.
// Semantic payload equality lives in MemoryPolicy and may differ from this low-level handle comparison.
inline bool same_value(const ValueRef& a, const ValueRef& b) {
    return a == b;
}

inline std::optional<std::string_view> get_value_view(const ValueStore& values, std::string_view key) {
    const auto it = values.find(Field(key));
    if (it == values.end() || !it->second) {
        return std::nullopt;
    }
    return std::string_view(*it->second);
}

template <class Policy>
inline std::optional<std::string_view> get_value_view(const BaseStore<Policy>& values, std::string_view key) {
    const auto it = values.find(Field(key));
    if (it == values.end() || !it->second) {
        return std::nullopt;
    }
    return Policy::to_debug_view(it->second);
}

template <class Policy>
inline std::optional<std::string_view> get_value_view(const OverlayStore<Policy>& values, std::string_view key) {
    const auto it = values.find(Field(key));
    if (it == values.end() || !it->second) {
        return std::nullopt;
    }
    return Policy::to_debug_view(it->second);
}

} // namespace proc
