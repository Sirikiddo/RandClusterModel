#pragma once

#include "ProcTypes.h"

#include <unordered_map>

namespace proc {

namespace detail {

template <class Policy, class Tag>
struct SparseStore {
    using Handle = typename Policy::Handle;
    using Map = std::unordered_map<Field, Handle>;
    using const_iterator = typename Map::const_iterator;

    Map kv;

    void clear() noexcept { kv.clear(); }
    [[nodiscard]] bool empty() const noexcept { return kv.empty(); }
    [[nodiscard]] bool contains(const Field& field) const { return kv.contains(field); }
    [[nodiscard]] const Handle& at(const Field& field) const { return kv.at(field); }
    [[nodiscard]] const_iterator begin() const noexcept { return kv.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return kv.end(); }
    [[nodiscard]] const_iterator find(const Field& field) const { return kv.find(field); }
};

struct base_store_tag final {};
struct overlay_store_tag final {};

} // namespace detail

template <class Policy>
struct BaseStore final : detail::SparseStore<Policy, detail::base_store_tag> {
    using detail::SparseStore<Policy, detail::base_store_tag>::SparseStore;
};

template <class Policy>
struct OverlayStore final : detail::SparseStore<Policy, detail::overlay_store_tag> {
    using detail::SparseStore<Policy, detail::overlay_store_tag>::SparseStore;
};

} // namespace proc
