#pragma once

#include "../core/GraphSchema.h"
#include "StateTypes.h"

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace proc {

struct DefaultMemoryPolicy {
    using Handle = ValueRef;
    using EqualFn = std::function<bool(const Handle&, const Handle&)>;
    using DebugStringFn = std::function<std::string(const Handle&)>;
    using ApplyDiffFn = std::function<Handle(const Handle&, const Handle&)>;

    DefaultMemoryPolicy() = default;

    explicit DefaultMemoryPolicy(const GraphSchema* schema)
        : schema_(schema) {
        bind_schema_algebras();
    }

    [[nodiscard]] Handle null_handle() const noexcept { return tombstone(); }
    [[nodiscard]] Handle clone(const Handle& handle) const { return duplicate(handle); }

    [[nodiscard]] bool equal(std::string_view field, const Handle& lhs, const Handle& rhs) const {
        if (!schema_) {
            return payload_equal(lhs, rhs);
        }

        const auto slot = schema_->find_field(field);
        if (!slot) {
            throw std::runtime_error("MemoryPolicy::equal references unknown field '" + std::string(field) + "'");
        }
        return equal(*slot, lhs, rhs);
    }

    [[nodiscard]] bool equal(v2::FieldSlot field_slot, const Handle& lhs, const Handle& rhs) const {
        if (!schema_) {
            return payload_equal(lhs, rhs);
        }

        const auto algebra_id = schema_->algebra_of(field_slot);
        if (not_has_algebra(algebra_id)) {
            return payload_equal(lhs, rhs);
        }
        return entry_of(algebra_id).equal(lhs, rhs);
    }

    [[nodiscard]] std::string debug_string(std::string_view field, const Handle& handle) const {
        if (!schema_) {
            return payload_debug_string(handle);
        }

        const auto slot = schema_->find_field(field);
        if (!slot) {
            throw std::runtime_error("MemoryPolicy::debug_string references unknown field '" + std::string(field) + "'");
        }
        return debug_string(*slot, handle);
    }

    [[nodiscard]] std::string debug_string(v2::FieldSlot field_slot, const Handle& handle) const {
        if (!schema_) {
            return payload_debug_string(handle);
        }

        const auto algebra_id = schema_->algebra_of(field_slot);
        if (not_has_algebra(algebra_id)) {
            return payload_debug_string(handle);
        }
        return entry_of(algebra_id).debug_string(handle);
    }

    [[nodiscard]] Handle apply_diff(v2::FieldSlot field_slot, const Handle& current, const Handle& diff) const {
        if (!schema_) {
            throw std::runtime_error("MemoryPolicy::apply_diff requires bound schema");
        }

        const auto algebra_id = schema_->algebra_of(field_slot);
        if (not_has_algebra(algebra_id)) {
            throw std::runtime_error("MemoryPolicy::apply_diff missing algebra binding for field slot");
        }
        const auto& entry = entry_of(algebra_id);
        if (!entry.apply_diff) {
            throw std::runtime_error("MemoryPolicy::apply_diff is not implemented for algebra id");
        }
        return entry.apply_diff(current, diff);
    }

    [[nodiscard]] Handle begin_mutation(v2::FieldSlot, const Handle& current) const {
        return clone(current);
    }

    [[nodiscard]] Handle finish_mutation(v2::FieldSlot, Handle mutated) const {
        return mutated;
    }

    static Handle tombstone() noexcept { return {}; }
    static bool is_tombstone(const Handle& handle) noexcept { return !handle; }
    static Handle duplicate(const Handle& handle) { return handle; }
    static bool same(const Handle& a, const Handle& b) noexcept { return same_value(a, b); }

    static std::optional<std::string_view> to_debug_view(const Handle& handle) {
        if (!handle) {
            return std::nullopt;
        }
        return std::string_view(*handle);
    }

    static Handle from_debug_string(std::string value) {
        return make_value(std::move(value));
    }

private:
    struct AlgebraEntry final {
        EqualFn equal;
        DebugStringFn debug_string;
        ApplyDiffFn apply_diff;
    };

    static bool payload_equal(const Handle& lhs, const Handle& rhs) noexcept {
        if (!lhs || !rhs) {
            return lhs == rhs;
        }
        return *lhs == *rhs;
    }

    static std::string payload_debug_string(const Handle& handle) {
        if (!handle) {
            return "<tombstone>";
        }
        return *handle;
    }

    [[nodiscard]] bool not_has_algebra(v2::AlgebraId id) const noexcept {
        return !algebras_.contains(id);
    }

    void register_algebra(
        v2::AlgebraId id,
        EqualFn equal_fn,
        DebugStringFn debug_string_fn,
        ApplyDiffFn apply_diff_fn = {}) {
        if (!equal_fn || !debug_string_fn) {
            throw std::runtime_error("DefaultMemoryPolicy requires equality and debug_string callables");
        }
        if (!algebras_.emplace(id, AlgebraEntry{std::move(equal_fn), std::move(debug_string_fn), std::move(apply_diff_fn)}).second) {
            throw std::runtime_error("DefaultMemoryPolicy duplicate algebra id");
        }
    }

    [[nodiscard]] const AlgebraEntry& entry_of(v2::AlgebraId id) const {
        const auto it = algebras_.find(id);
        if (it == algebras_.end()) {
            throw std::runtime_error("DefaultMemoryPolicy unknown algebra id");
        }
        return it->second;
    }

    void bind_schema_algebras() {
        if (!schema_) {
            return;
        }

        for (std::size_t i = 0; i < schema_->field_count(); ++i) {
            const auto algebra_id = schema_->algebra_of(static_cast<v2::FieldSlot>(i));
            if (algebras_.contains(algebra_id)) {
                continue;
            }

            register_algebra(
                algebra_id,
                [](const Handle& lhs, const Handle& rhs) {
                    return payload_equal(lhs, rhs);
                },
                [](const Handle& handle) {
                    return payload_debug_string(handle);
                },
                [algebra_id](const Handle&, const Handle&) -> Handle {
                    throw std::runtime_error(
                        "MemoryPolicy::apply_diff is not implemented for algebra id " +
                        std::to_string(static_cast<std::size_t>(algebra_id)));
                });
        }
    }

    const GraphSchema* schema_ = nullptr;
    std::unordered_map<v2::AlgebraId, AlgebraEntry> algebras_;
};

} // namespace proc
