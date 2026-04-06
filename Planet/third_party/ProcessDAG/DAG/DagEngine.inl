#pragma once

#include "DagStorage.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace proc {
namespace detail {

inline void validate_schema_or_throw(const GraphSchema& schema) {
    if (schema.field_count() == 0 && schema.node_count() == 0) {
        throw std::runtime_error("DagEngine requires non-empty schema");
    }
}

inline void validate_runtime_bindings_or_throw(const GraphSchema& schema, const RuntimeOperationRegistry& operations) {
    for (std::size_t i = 0; i < schema.node_count(); ++i) {
        const auto node_slot = static_cast<v2::NodeSlot>(i);
        const auto op_id = schema.op_of(node_slot);
        if (!operations.contains(op_id)) {
            throw std::runtime_error(
                "compiled schema references op id that is absent in runtime registry for node '" +
                std::string(schema.node_name(node_slot)) + "'");
        }
        if (!operations.has_binding(op_id, node_slot)) {
            throw std::runtime_error(
                "runtime registry missing executable binding for node '" + std::string(schema.node_name(node_slot)) + "'");
        }
    }
}

template <class ImplT>
std::unique_ptr<ImplT> make_engine_impl(
    GraphSchema schema,
    RuntimeOperationRegistry operation_registry,
    GuardRegistry guard_registry,
    FlushFailurePolicy failure_policy) {
    return std::make_unique<ImplT>(
        std::move(schema),
        std::move(operation_registry),
        std::move(guard_registry),
        failure_policy);
}

template <class ImplT>
std::unique_ptr<ImplT> make_default_engine_impl(GraphSchema schema, FlushFailurePolicy failure_policy) {
    auto operations = RuntimeOperationRegistry::make_builtin_synthetic(schema);
    return make_engine_impl<ImplT>(std::move(schema), std::move(operations), make_builtin_guard_registry(), failure_policy);
}

template <class CommitT>
CommitT make_init_commit(const ValueStore& init_snapshot) {
    CommitT initial_inputs;
    initial_inputs.reserve(init_snapshot.size());
    for (const auto& [field, value] : init_snapshot) {
        if (value) {
            initial_inputs.set(field, *value);
        }
    }
    return initial_inputs;
}

} // namespace detail

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
struct DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::Impl final {
    using RuntimeStorage = Storage;

    Impl(
        GraphSchema schema_in,
        RuntimeOperationRegistry operation_registry_in,
        GuardRegistry guard_registry_in,
        FlushFailurePolicy failure_policy_in)
        : operation_registry(std::move(operation_registry_in)),
          schema(std::move(schema_in)),
          roles(schema.storage_layout()),
          storage(roles),
          memory_policy(&schema),
          planner(schema),
          guard_registry(std::move(guard_registry_in)),
          failure_policy(failure_policy_in) {
        detail::validate_schema_or_throw(schema);
        detail::validate_runtime_bindings_or_throw(schema, operation_registry);
    }

    Plan plan_from_changed(const FieldSet& changed_fields) const {
        return planner.describe(planner.build_plan(changed_fields, {}));
    }

    void reset_runtime() {
        storage = RuntimeStorage(roles);
        prepared_pending_ack = false;
    }

    void validate_output_request(const std::vector<Field>& outputs) const {
        for (const auto& field : outputs) {
            if (roles.is_output(field)) continue;
            throw std::runtime_error("flush_prepare requested non-output field '" + field + "'");
        }
    }

    RuntimeOperationRegistry operation_registry;
    GraphSchema schema;
    typename Storage::Roles roles;
    RuntimeStorage storage;
    Policy memory_policy;
    Planner planner;
    GuardRegistry guard_registry;
    Executor executor;
    FlushFailurePolicy failure_policy = FlushFailurePolicy::InvalidateAllInputs;
    bool initialized = false;
    bool prepared_pending_ack = false;
};

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::DagEngine(GraphSchema schema, FlushFailurePolicy failure_policy) {
    impl_ = detail::make_default_engine_impl<Impl>(std::move(schema), failure_policy);
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::DagEngine(
    GraphSchema schema,
    RuntimeOperationRegistry operation_registry,
    GuardRegistry guard_registry,
    FlushFailurePolicy failure_policy) {
    impl_ = detail::make_engine_impl<Impl>(
        std::move(schema),
        std::move(operation_registry),
        std::move(guard_registry),
        failure_policy);
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::~DagEngine() = default;

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::DagEngine(DagEngine&&) noexcept = default;

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>&
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::operator=(DagEngine&&) noexcept = default;

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
void DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::init(const ValueStore& init_snapshot) {
    impl_->reset_runtime();
    auto initial_inputs = detail::make_init_commit<CommitT>(init_snapshot);
    impl_->storage.push_input(initial_inputs, impl_->memory_policy, &impl_->schema);
    impl_->initialized = true;
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
void DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::push_input(const CommitT& c) {
    if (!impl_->initialized) throw std::runtime_error("DagEngine::push_input called before init");
    impl_->storage.push_input(c, impl_->memory_policy, &impl_->schema);
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
bool DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::flush_prepare(const std::vector<Field>& outputs) {
    if (!impl_->initialized) throw std::runtime_error("DagEngine::flush_prepare called before init");
    if (outputs.empty()) throw std::runtime_error("flush_prepare requires explicit outputs");
    impl_->validate_output_request(outputs);

    const auto dirty_inputs = impl_->storage.pending_input_fields();
    if (dirty_inputs.empty()) {
        return false;
    }

    const auto plan = impl_->planner.build_plan(dirty_inputs, outputs);

    try {
        impl_->storage.begin_run();
        try {
            impl_->executor.run(
                plan,
                impl_->schema,
                impl_->storage,
                impl_->memory_policy,
                impl_->operation_registry,
                impl_->guard_registry,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr);
            impl_->storage.end_run_success();
        } catch (...) {
            if (impl_->storage.is_dag_open()) {
                impl_->storage.end_run_abort();
            }
            throw;
        }

        impl_->storage.prepare_outputs(outputs, true);
        impl_->prepared_pending_ack = true;
    } catch (const std::exception&) {
        if (impl_->failure_policy == FlushFailurePolicy::InvalidateAllInputs) {
            impl_->storage.invalidate_all_inputs_for_retry();
        }
        throw;
    }

    return true;
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
bool DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::ack_outputs() {
    if (!impl_->initialized) throw std::runtime_error("DagEngine::ack_outputs called before init");

    if (!impl_->prepared_pending_ack) {
        return false;
    }

    impl_->storage.ack_outputs();
    impl_->prepared_pending_ack = false;
    return true;
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
typename DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::Plan
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::plan_from_changed(const FieldSet& changed_fields) const {
    return impl_->plan_from_changed(changed_fields);
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
FieldSet DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::dirty_inputs() const {
    return impl_->storage.pending_input_fields();
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
ValueStore DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::input_snapshot() const {
    return impl_->storage.input_snapshot();
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
const typename DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::PreparedStore&
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::prepared_output_store() const noexcept {
    static const PreparedStore empty;
    if (!impl_->prepared_pending_ack) {
        return empty;
    }
    return impl_->storage.prepared_outputs();
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
const typename DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::PublishedStore&
DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::published_output_store() const noexcept {
    return impl_->storage.published_outputs();
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
bool DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::has_prepared_outputs() const noexcept {
    return impl_->prepared_pending_ack;
}

template <class Policy, template <class> class BaseStoreT, template <class> class OverlayStoreT, class CommitT>
void DagEngine<Policy, BaseStoreT, OverlayStoreT, CommitT>::dump_graph() const {
    std::cout << "DagEngine nodes:\n";
    for (const auto& id : impl_->planner.node_names()) {
        std::cout << "  - " << id << "\n";
    }
    std::cout << "Dependencies (A -> B means A runs before B):\n";
    for (const auto& [from, to] : impl_->planner.dependency_edges()) {
        std::cout << "  " << from << " -> " << to << "\n";
    }
}

} // namespace proc
