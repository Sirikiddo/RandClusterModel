#pragma once

#include "ProcTypes.h"
#include "StoreTypes.h"
#include "MemoryPolicy.h"
#include <type_traits>
#include <utility>

namespace proc {

// Layer protocol:
// - D: current draft overlay.
// - V: current stable snapshot.
// - G: last known good / archival snapshot when present.
// Read precedence is D -> V -> G. Tombstone means explicit delete of lower layers.

template <int lvl, class Policy = DefaultMemoryPolicy, template <class> class BaseStoreT = BaseStore, template <class> class OverlayStoreT = OverlayStore>
class LayeredState;

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT>
class LayeredState<1, Policy, BaseStoreT, OverlayStoreT> {
public:
    static constexpr int level = 1;
    using Handle = typename Policy::Handle;
    using Overlay = OverlayStoreT<Policy>;

    [[nodiscard]] Handle get(const Field& field) const {
        if (const auto it = D_.kv.find(field); it != D_.kv.end()) {
            return it->second;
        }
        return Policy::tombstone();
    }

    void set_D(Field field, Handle handle) {
        D_.kv[std::move(field)] = std::move(handle);
    }

    void erase_D(Field field) {
        D_.kv[std::move(field)] = Policy::tombstone();
    }

    void drop_D(const Field& field) {
        D_.kv.erase(field);
    }

    void clear_D() noexcept { D_.clear(); }
    void rollback_local() noexcept { D_.clear(); }

    [[nodiscard]] const Overlay& D() const noexcept { return D_; }
    [[nodiscard]] Overlay& D_mut() noexcept { return D_; }

private:
    Overlay D_;
};

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT>
class LayeredState<2, Policy, BaseStoreT, OverlayStoreT> {
public:
    static constexpr int level = 2;
    using Handle = typename Policy::Handle;
    using Overlay = OverlayStoreT<Policy>;
    using Base = BaseStoreT<Policy>;

    [[nodiscard]] Handle get(const Field& field) const {
        if (const auto d = D_.kv.find(field); d != D_.kv.end()) return d->second;
        if (const auto v = V_.kv.find(field); v != V_.kv.end()) return v->second;
        return Policy::tombstone();
    }

    void set_D(Field field, Handle handle) {
        D_.kv[std::move(field)] = std::move(handle);
    }

    void erase_D(Field field) {
        D_.kv[std::move(field)] = Policy::tombstone();
    }

    void drop_D(const Field& field) {
        D_.kv.erase(field);
    }

    void clear_D() noexcept { D_.clear(); }

    void promote_D_to_V_apply() {
        for (const auto& [field, handle] : D_.kv) {
            if (Policy::is_tombstone(handle)) {
                V_.kv.erase(field);
            } else {
                V_.kv.insert_or_assign(field, Policy::duplicate(handle));
            }
        }
        D_.clear();
    }

    void rollback_local() noexcept { D_.clear(); }

    [[nodiscard]] const Overlay& D() const noexcept { return D_; }
    [[nodiscard]] const Base& V() const noexcept { return V_; }
    [[nodiscard]] Overlay& D_mut() noexcept { return D_; }
    [[nodiscard]] Base& V_mut() noexcept { return V_; }

private:
    Overlay D_;
    Base V_;
};

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT>
class LayeredState<3, Policy, BaseStoreT, OverlayStoreT> {
public:
    static constexpr int level = 3;
    using Handle = typename Policy::Handle;
    using Overlay = OverlayStoreT<Policy>;
    using Base = BaseStoreT<Policy>;

    [[nodiscard]] Handle get(const Field& field) const {
        if (const auto d = D_.kv.find(field); d != D_.kv.end()) return d->second;
        if (const auto v = V_.kv.find(field); v != V_.kv.end()) return v->second;
        if (const auto g = G_.kv.find(field); g != G_.kv.end()) return g->second;
        return Policy::tombstone();
    }

    void set_D(Field field, Handle handle) {
        D_.kv[std::move(field)] = std::move(handle);
    }

    void erase_D(Field field) {
        D_.kv[std::move(field)] = Policy::tombstone();
    }

    void drop_D(const Field& field) {
        D_.kv.erase(field);
    }

    void clear_D() noexcept { D_.clear(); }

    void promote_D_to_V_apply() {
        for (const auto& [field, handle] : D_.kv) {
            if (Policy::is_tombstone(handle)) {
                V_.kv.erase(field);
            } else {
                V_.kv.insert_or_assign(field, Policy::duplicate(handle));
            }
        }
        D_.clear();
    }

    void rollback_local() noexcept { D_.clear(); }

    [[nodiscard]] const Overlay& D() const noexcept { return D_; }
    [[nodiscard]] const Base& V() const noexcept { return V_; }
    [[nodiscard]] const Base& G() const noexcept { return G_; }
    [[nodiscard]] Overlay& D_mut() noexcept { return D_; }
    [[nodiscard]] Base& V_mut() noexcept { return V_; }
    [[nodiscard]] Base& G_mut() noexcept { return G_; }

private:
    Overlay D_;
    Base V_;
    Base G_;
};

static_assert(std::is_default_constructible_v<LayeredState<1>>);
static_assert(std::is_default_constructible_v<LayeredState<2>>);
static_assert(std::is_default_constructible_v<LayeredState<3>>);

} // namespace proc
