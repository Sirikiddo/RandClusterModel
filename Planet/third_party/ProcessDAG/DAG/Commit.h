#pragma once

#include "CoreV2.h"
#include "ProcTypes.h"
#include "StateTypes.h"
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace proc {

class GraphSchema;

struct Commit final {
    using Handle = ValueRef;

    class ChangeView final {
    public:
        ChangeView() = default;

        [[nodiscard]] bool has_field_slot() const noexcept { return field_slot_ != invalid_field_slot(); }
        [[nodiscard]] v2::FieldSlot field_slot() const noexcept { return field_slot_; }
        [[nodiscard]] std::string_view field_name() const noexcept { return field_name_; }
        [[nodiscard]] v2::ChangeKind kind() const noexcept { return kind_; }
        [[nodiscard]] v2::WriteLifetime lifetime() const noexcept { return lifetime_; }
        [[nodiscard]] const Handle& payload() const noexcept { return *payload_; }

    private:
        friend struct Commit;

        ChangeView(
            v2::FieldSlot field_slot,
            std::string_view field_name,
            v2::ChangeKind kind,
            v2::WriteLifetime lifetime,
            const Handle* payload) noexcept
            : field_slot_(field_slot), field_name_(field_name), kind_(kind), lifetime_(lifetime), payload_(payload) {}

        static constexpr v2::FieldSlot invalid_field_slot() noexcept {
            return std::numeric_limits<v2::FieldSlot>::max();
        }

        v2::FieldSlot field_slot_ = invalid_field_slot();
        std::string_view field_name_;
        v2::ChangeKind kind_{v2::ChangeKind::Tombstone};
        v2::WriteLifetime lifetime_{v2::WriteLifetime::Persistent};
        const Handle* payload_ = nullptr;
    };

    bool its_time = false;

    bool empty() const noexcept;
    void reserve(size_t n);
    [[nodiscard]] std::size_t change_count() const noexcept;

    template <class Fn>
    void for_each_change(Fn&& fn) const {
        for (const auto& change : changes_) {
            fn(ChangeView{
                change.field,
                change.field_name,
                change.kind,
                change.lifetime,
                &change.payload,
            });
        }
    }

    void set(v2::FieldSlot key, Str value, v2::WriteLifetime lifetime = v2::WriteLifetime::Persistent, std::string debug_name = {});
    void set_handle(
        v2::FieldSlot key,
        Handle value,
        v2::WriteLifetime lifetime = v2::WriteLifetime::Persistent,
        std::string debug_name = {});
    void set(Field key, Str payload, v2::WriteLifetime lifetime = v2::WriteLifetime::Persistent);
    void set_handle(Field key, Handle payload, v2::WriteLifetime lifetime = v2::WriteLifetime::Persistent);
    void erase(v2::FieldSlot key, v2::WriteLifetime lifetime = v2::WriteLifetime::Persistent, std::string debug_name = {});
    void erase(Field key, v2::WriteLifetime lifetime = v2::WriteLifetime::Persistent);

    void merge_from(const Commit& other);
    [[nodiscard]] Commit resolved(const GraphSchema& schema) const;

    static std::string_view debug_view(const Handle& handle);
    static std::string field_debug_name(const ChangeView& change, const GraphSchema* schema = nullptr);

private:
    static constexpr v2::FieldSlot invalid_field_slot() noexcept {
        return std::numeric_limits<v2::FieldSlot>::max();
    }

    struct ChangeRecord final {
        v2::FieldSlot field{invalid_field_slot()};
        std::string field_name;
        v2::ChangeKind kind{v2::ChangeKind::Tombstone};
        v2::WriteLifetime lifetime{v2::WriteLifetime::Persistent};
        Handle payload{};

        [[nodiscard]] bool has_field_slot() const noexcept {
            return field != invalid_field_slot();
        }
    };

    static ChangeRecord make_change(
        v2::FieldSlot field_slot,
        std::string field_name,
        v2::ChangeKind kind,
        v2::WriteLifetime lifetime,
        Handle payload = {});
    static ChangeRecord make_change(Field field_name, v2::ChangeKind kind, v2::WriteLifetime lifetime, Handle payload = {});
    static ChangeRecord make_change(const ChangeView& change);

    void upsert_change(ChangeRecord next);
    ChangeRecord* find_change(const ChangeRecord& change) noexcept;
    ChangeRecord* find_unresolved_change(std::string_view field) noexcept;

    std::vector<ChangeRecord> changes_;
};

} // namespace proc

