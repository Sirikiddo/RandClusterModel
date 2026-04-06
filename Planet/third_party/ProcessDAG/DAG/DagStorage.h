#pragma once

#include "Commit.h"
#include "PortStates.h"
#include "../core/GraphSchema.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace proc {

enum class WorkRollbackMode {
    Local,
    Global,
};

// DagStorage owns the protocol state of one DAG instance.
// - St_I: staged next-run input in D, frozen current-run input in G.
// - St_S: durable work state. lvl2 gives local rollback (drop D -> reveal V),
//   lvl3 gives global rollback (restore V from the begin_run() baseline in G).
// - St_O: prepared output frame in V and published output frame in G.
// internal_ephemeral is never a separate store: it lives in S and is sweep-cleaned from
// persisted S layers on begin/end so it never survives a run boundary.
template <
    WorkRollbackMode Mode,
    class Policy = DefaultMemoryPolicy,
    template <class> class BaseStoreT = BaseStore,
    template <class> class OverlayStoreT = OverlayStore,
    class CommitT = Commit>
struct DagStorage final {
    static constexpr int work_level = Mode == WorkRollbackMode::Global ? 3 : 2;

    using Handle = typename Policy::Handle;
    using Roles = GraphSchema::StorageLayout;
    using InputPort = InputPortState<Policy, BaseStoreT, OverlayStoreT>;
    using WorkPort = WorkPortState<work_level, Policy, BaseStoreT, OverlayStoreT>;
    using OutputPort = OutputPortState<Policy, BaseStoreT, OverlayStoreT>;
    using PreparedStore = typename OutputPort::Base;
    using PublishedStore = typename OutputPort::Base;
    using InputStore = typename InputPort::Base;

    Roles roles;
    InputPort St_I;
    WorkPort St_S;
    OutputPort St_O;
    bool dag_open = false;

    DagStorage() = default;

    explicit DagStorage(Roles roles_in) : roles(std::move(roles_in)) {
        roles.validate_disjoint_or_throw();
    }

    [[nodiscard]] bool is_dag_open() const noexcept { return dag_open; }

    void assert_can_begin() const {
        assert(!dag_open && "DagStorage::begin_run requires a closed DAG; finish the previous run with end_run_success/end_run_abort first");
    }

    void assert_can_end() const {
        assert(dag_open && "DagStorage::end_run_* requires begin_run() to open the current DAG run");
    }

    [[nodiscard]] Handle read_node_handle(v2::FieldSlot field_slot, const GraphSchema& schema) const {
        return read_visible_handle(schema.field_key(field_slot));
    }

    [[nodiscard]] Handle read_visible_handle(const Field& field) const {
        if (roles.is_input(field)) {
            return St_I.get(field);
        }
        return St_S.get(field);
    }

    [[nodiscard]] Handle read_prepared_output_handle(const Field& field) const {
        if (const auto it = St_O.V().kv.find(field); it != St_O.V().kv.end()) {
            return it->second;
        }
        return Policy::tombstone();
    }

    [[nodiscard]] Handle read_published_output_handle(const Field& field) const {
        if (const auto it = St_O.G().kv.find(field); it != St_O.G().kv.end()) {
            return it->second;
        }
        return Policy::tombstone();
    }

    [[nodiscard]] ValueStore input_snapshot() const { return collect_visible_values(St_I); }

    [[nodiscard]] const PreparedStore& prepared_outputs() const noexcept { return St_O.V(); }
    [[nodiscard]] const PublishedStore& published_outputs() const noexcept { return St_O.G(); }

    template <class AnyCommit>
    void push_input(const AnyCommit& commit, const Policy& memory_policy, const GraphSchema* schema = nullptr) {
        require_closed_run("DagStorage::push_input");
        apply_commit_changes(
            commit,
            memory_policy,
            schema,
            "DagStorage::push_input",
            [this](const Field& field, const char* where) { validate_input_field(field, where); },
            [this](const Field& field) { return St_I.get(field); },
            [this](const Field& field, Handle value) { St_I.set_D(field, std::move(value)); },
            [this](const Field& field) { St_I.erase_D(field); },
            false);
    }

    template <class AnyCommit>
    void apply_node_commit(const AnyCommit& commit, const Policy& memory_policy, const GraphSchema* schema = nullptr) {
        require_open_run("DagStorage::apply_node_commit");
        apply_commit_changes(
            commit,
            memory_policy,
            schema,
            "DagStorage::apply_node_commit",
            [this](const Field& field, const char* where) { validate_non_input_field(field, where); },
            [this](const Field& field) { return St_S.get(field); },
            [this](const Field& field, Handle value) { St_S.set_D(field, std::move(value)); },
            [this](const Field& field) { St_S.erase_D(field); },
            true);
    }

    void begin_run() {
        roles.validate_disjoint_or_throw();
        assert_can_begin();
        cleanup_internal_ephemeral();
        if constexpr (Mode == WorkRollbackMode::Global) {
            capture_visible_work_to_good();
        }
        St_S.clear_D();
        dag_open = true;
        freeze_inputs();
    }

    void end_run_success() {
        assert_can_end();
        St_S.promote_D_to_V_apply();
        cleanup_internal_ephemeral();
        dag_open = false;
    }

    void end_run_abort() {
        assert_can_end();
        if constexpr (Mode == WorkRollbackMode::Global) {
            rollback_global_work();
        } else {
            St_S.rollback_local();
        }
        cleanup_internal_ephemeral();
        dag_open = false;
    }

    [[nodiscard]] FieldSet pending_input_fields() const {
        FieldSet pending;
        for (const auto& [field, _] : St_I.D().kv) {
            pending.insert(field);
        }
        return pending;
    }

    void invalidate_all_inputs_for_retry() {
        require_closed_run("DagStorage::invalidate_all_inputs_for_retry");
        St_I.clear_D();
        for (const auto& field : roles.inputs) {
            const auto frozen = St_I.G().kv.find(field);
            if (frozen == St_I.G().kv.end()) {
                St_I.erase_D(field);
            } else {
                St_I.set_D(field, Policy::duplicate(frozen->second));
            }
        }
    }

    void prepare_outputs(const std::vector<Field>& outputs, bool prefer_committed_V = true) {
        St_O.clear_D();

        for (const auto& field : outputs) {
            if (!roles.is_output(field)) {
                throw std::runtime_error("DagStorage::prepare_outputs requested non-output field '" + field + "'");
            }

            Handle handle = prefer_committed_V ? committed_work_handle(field) : St_S.get(field);
            if (Policy::is_tombstone(handle)) {
                continue;
            }

            St_O.set_D(field, Policy::duplicate(handle));
        }

        St_O.promote_D_to_V_apply();
    }

    void ack_outputs() {
        require_closed_run("DagStorage::ack_outputs");
        St_O.G_mut().kv = St_O.V().kv;
    }

    [[nodiscard]] FieldSet diff_output_frames(const Policy& memory_policy, const std::vector<Field>& outputs) const {
        FieldSet dirty;
        for (const auto& field : outputs) {
            const auto prepared_ref = read_prepared_output_handle(field);
            const auto published_ref = read_published_output_handle(field);
            if (!memory_policy.equal(field, prepared_ref, published_ref)) {
                dirty.insert(field);
            }
        }
        return dirty;
    }

    [[nodiscard]] FieldSet diff_all_output_frames(const Policy& memory_policy) const {
        FieldSet keys;
        for (const auto& [field, _] : St_O.V().kv) keys.insert(field);
        for (const auto& [field, _] : St_O.G().kv) keys.insert(field);
        return diff_output_frames(memory_policy, std::vector<Field>(keys.begin(), keys.end()));
    }

private:
    void require_open_run(const char* where) const {
        if (!dag_open) {
            throw std::runtime_error(std::string(where) + " requires begin_run() to open a DAG run first");
        }
    }

    void require_closed_run(const char* where) const {
        if (dag_open) {
            throw std::runtime_error(std::string(where) + " requires a closed DAG run");
        }
    }

    void validate_input_field(const Field& field, const char* where) const {
        if (roles.is_input(field)) return;
        throw std::runtime_error(std::string(where) + " attempted to mutate non-input field '" + field + "'");
    }

    void validate_non_input_field(const Field& field, const char* where) const {
        if (!roles.is_input(field)) return;
        throw std::runtime_error(std::string(where) + " attempted to mutate input field '" + field + "'");
    }

    static void validate_supported_change(const typename CommitT::ChangeView&, const char*) {
    }

    static Field resolve_field_name(const typename CommitT::ChangeView& change, const GraphSchema* schema) {
        if (change.has_field_slot()) {
            if (!schema) {
                throw std::runtime_error(
                    "DagStorage requires bound schema to apply slot-addressed commit field '" +
                    Commit::field_debug_name(change) + "'");
            }
            return schema->field_key(change.field_slot());
        }
        return Field(change.field_name());
    }

    static v2::FieldSlot resolve_field_slot(
        const typename CommitT::ChangeView& change,
        const Field& field,
        const GraphSchema* schema,
        const char* where) {
        if (change.has_field_slot()) {
            return change.field_slot();
        }
        if (!schema) {
            throw std::runtime_error(std::string(where) + " requires bound schema to resolve field slot for '" + field + "'");
        }
        const auto slot = schema->find_field(field);
        if (!slot) {
            throw std::runtime_error(std::string(where) + " references unknown field '" + field + "'");
        }
        return *slot;
    }

    static Handle materialize_payload(
        const typename CommitT::ChangeView& change,
        const Field& field,
        const Handle& current,
        const Policy& memory_policy,
        const GraphSchema* schema,
        const char* where) {
        if (change.kind() == v2::ChangeKind::ApplyDiff) {
            const auto field_slot = resolve_field_slot(change, field, schema, where);
            return memory_policy.apply_diff(field_slot, current, change.payload());
        }
        return Policy::duplicate(change.payload());
    }

    static bool should_skip_set(
        const Policy& memory_policy,
        const typename CommitT::ChangeView& change,
        const Field& field,
        const Handle& current,
        const Handle& next_value) {
        if (Policy::is_tombstone(current)) return false;
        if (change.has_field_slot()) {
            return memory_policy.equal(change.field_slot(), current, next_value);
        }
        return memory_policy.equal(field, current, next_value);
    }

    template <class AnyCommit, class ValidateFieldFn, class CurrentHandleFn, class SetFn, class EraseFn>
    void apply_commit_changes(
        const AnyCommit& commit,
        const Policy& memory_policy,
        const GraphSchema* schema,
        const char* where,
        ValidateFieldFn&& validate_field,
        CurrentHandleFn&& current_handle_for,
        SetFn&& write_set,
        EraseFn&& write_erase,
        bool allow_skip_equal) {
        commit.for_each_change([&](const auto& change) {
            const auto field = resolve_field_name(change, schema);
            validate_field(field, where);
            validate_supported_change(change, where);

            const auto current = current_handle_for(field);
            auto next_value = materialize_payload(change, field, current, memory_policy, schema, where);
            const bool is_tombstone = change.kind() == v2::ChangeKind::Tombstone || Policy::is_tombstone(next_value);
            if (!is_tombstone && allow_skip_equal && should_skip_set(memory_policy, change, field, current, next_value)) {
                return;
            }

            if (is_tombstone) {
                write_erase(field);
            } else {
                write_set(field, std::move(next_value));
            }
        });
    }

    template <class Layer>
    static void append_keys(FieldSet& keys, const Layer& layer) {
        for (const auto& [field, _] : layer.kv) {
            keys.insert(field);
        }
    }

    template <class Layer>
    static ValueStore collect_visible_values(const Layer& layer) {
        FieldSet keys;
        append_keys(keys, layer.D());
        if constexpr (Layer::level >= 2) append_keys(keys, layer.V());
        if constexpr (Layer::level >= 3) append_keys(keys, layer.G());

        ValueStore out;
        for (const auto& field : keys) {
            const auto handle = layer.get(field);
            if (!Policy::is_tombstone(handle)) {
                out[field] = handle;
            }
        }
        return out;
    }

    static ValueStore collect_visible_values(const BaseStoreT<Policy>& layer) {
        ValueStore out;
        for (const auto& [field, handle] : layer.kv) {
            if (!Policy::is_tombstone(handle)) {
                out[field] = handle;
            }
        }
        return out;
    }

    template <class SourceStore>
    static void apply_overlay_to_store(BaseStoreT<Policy>& destination, const SourceStore& overlay) {
        for (const auto& [field, handle] : overlay.kv) {
            if (Policy::is_tombstone(handle)) {
                destination.kv.erase(field);
            } else {
                destination.kv.insert_or_assign(field, Policy::duplicate(handle));
            }
        }
    }

    void freeze_inputs() {
        apply_overlay_to_store(St_I.G_mut(), St_I.D());
        St_I.clear_D();
        St_I.V_mut().clear();
    }

    void capture_visible_work_to_good() {
        BaseStoreT<Policy> snapshot;
        snapshot.kv = St_S.G().kv;
        apply_overlay_to_store(snapshot, St_S.V());
        apply_overlay_to_store(snapshot, St_S.D());
        St_S.G_mut() = std::move(snapshot);
    }

    void rollback_global_work() {
        St_S.V_mut().kv = St_S.G().kv;
        St_S.clear_D();
    }

    [[nodiscard]] Handle committed_work_handle(const Field& field) const {
        if (const auto it = St_S.V().kv.find(field); it != St_S.V().kv.end()) {
            return it->second;
        }
        return Policy::tombstone();
    }

    template <class Layer>
    void erase_internal_from_layer(Layer& layer) {
        for (auto it = layer.kv.begin(); it != layer.kv.end();) {
            if (roles.is_internal_ephemeral(it->first)) {
                it = layer.kv.erase(it);
            } else {
                ++it;
            }
        }
    }

    void cleanup_internal_ephemeral() {
        erase_internal_from_layer(St_S.D_mut());
        erase_internal_from_layer(St_S.V_mut());
        if constexpr (Mode == WorkRollbackMode::Global) {
            erase_internal_from_layer(St_S.G_mut());
        }
    }
};

} // namespace proc
