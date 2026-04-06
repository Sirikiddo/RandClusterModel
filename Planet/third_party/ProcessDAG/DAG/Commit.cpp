#include "Commit.h"

#include "../core/GraphSchema.h"

#include <stdexcept>
#include <utility>

namespace proc {

bool Commit::empty() const noexcept { return changes_.empty(); }

void Commit::reserve(size_t n) { changes_.reserve(n); }

std::size_t Commit::change_count() const noexcept { return changes_.size(); }

Commit::ChangeRecord Commit::make_change(
    v2::FieldSlot field_slot,
    std::string field_name,
    v2::ChangeKind kind,
    v2::WriteLifetime lifetime,
    Handle payload) {
    return ChangeRecord{
        field_slot,
        std::move(field_name),
        kind,
        lifetime,
        std::move(payload),
    };
}

Commit::ChangeRecord Commit::make_change(Field field_name, v2::ChangeKind kind, v2::WriteLifetime lifetime, Handle payload) {
    return make_change(invalid_field_slot(), std::move(field_name), kind, lifetime, std::move(payload));
}

Commit::ChangeRecord Commit::make_change(const ChangeView& change) {
    return make_change(
        change.has_field_slot() ? change.field_slot() : invalid_field_slot(),
        std::string(change.field_name()),
        change.kind(),
        change.lifetime(),
        change.payload());
}

void Commit::upsert_change(ChangeRecord next) {
    if (auto* existing = find_change(next)) {
        *existing = std::move(next);
        return;
    }

    changes_.push_back(std::move(next));
}

void Commit::set(v2::FieldSlot key, Str value, v2::WriteLifetime lifetime, std::string debug_name) {
    set_handle(key, make_value(std::move(value)), lifetime, std::move(debug_name));
}

void Commit::set_handle(v2::FieldSlot key, Handle value, v2::WriteLifetime lifetime, std::string debug_name) {
    upsert_change(make_change(key, std::move(debug_name), v2::ChangeKind::SetValue, lifetime, std::move(value)));
}

void Commit::set(Field key, Str payload, v2::WriteLifetime lifetime) {
    set_handle(std::move(key), make_value(std::move(payload)), lifetime);
}

void Commit::set_handle(Field key, Handle payload, v2::WriteLifetime lifetime) {
    upsert_change(make_change(std::move(key), v2::ChangeKind::SetValue, lifetime, std::move(payload)));
}

void Commit::erase(v2::FieldSlot key, v2::WriteLifetime lifetime, std::string debug_name) {
    upsert_change(make_change(key, std::move(debug_name), v2::ChangeKind::Tombstone, lifetime));
}

void Commit::erase(Field key, v2::WriteLifetime lifetime) {
    upsert_change(make_change(std::move(key), v2::ChangeKind::Tombstone, lifetime));
}

void Commit::merge_from(const Commit& other) {
    other.for_each_change([&](const ChangeView& change) {
        upsert_change(make_change(change));
    });
}

Commit Commit::resolved(const GraphSchema& schema) const {
    Commit out;
    out.reserve(changes_.size());

    for (const auto& change : changes_) {
        ChangeRecord resolved_change = change;
        if (!resolved_change.has_field_slot()) {
            const auto resolved_slot = schema.find_field(resolved_change.field_name);
            if (!resolved_slot) {
                throw std::runtime_error("Commit references unknown field '" + resolved_change.field_name + "'");
            }
            resolved_change.field = *resolved_slot;
        }
        if (resolved_change.field_name.empty()) {
            resolved_change.field_name = std::string(schema.field_name(resolved_change.field));
        }

        out.upsert_change(std::move(resolved_change));
    }

    out.its_time = its_time;
    return out;
}

std::string_view Commit::debug_view(const Handle& handle) {
    if (!handle) {
        return {};
    }
    return *handle;
}

std::string Commit::field_debug_name(const ChangeView& change, const GraphSchema* schema) {
    if (!change.field_name().empty()) {
        return std::string(change.field_name());
    }
    if (schema && change.has_field_slot()) {
        return std::string(schema->field_name(change.field_slot()));
    }
    if (change.has_field_slot()) {
        return "slot#" + std::to_string(static_cast<std::size_t>(change.field_slot()));
    }
    return "<unknown-field>";
}

Commit::ChangeRecord* Commit::find_change(const ChangeRecord& change) noexcept {
    if (!change.has_field_slot()) {
        return find_unresolved_change(change.field_name);
    }

    for (auto& existing : changes_) {
        if (existing.has_field_slot() && existing.field == change.field) {
            return &existing;
        }
    }
    return nullptr;
}

Commit::ChangeRecord* Commit::find_unresolved_change(std::string_view field) noexcept {
    for (auto& change : changes_) {
        if (!change.has_field_slot() && change.field_name == field) {
            return &change;
        }
    }
    return nullptr;
}

} // namespace proc

