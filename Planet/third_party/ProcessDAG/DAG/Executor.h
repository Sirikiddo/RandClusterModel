#pragma once

#include "DagStorage.h"
#include "RuntimeOperationRegistry.h"
#include "../core/GraphSchema.h"
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace proc {

class GuardRegistry final {
public:
    using CurrentHandle = Commit::Handle;
    using EqualFn = std::function<bool(v2::FieldSlot, const CurrentHandle&, const CurrentHandle&)>;
    using ArgumentFactoryFn = std::function<CurrentHandle(const std::string&)>;
    using EvalFn =
        std::function<bool(v2::FieldSlot, const CurrentHandle&, const std::string&, const EqualFn&, const ArgumentFactoryFn&)>;

    void register_predicate(v2::GuardPredicateId predicate_id, EvalFn eval_fn) {
        if (!eval_fn) {
            throw std::runtime_error("GuardRegistry::register_predicate requires a callable");
        }
        if (!eval_by_id_.emplace(predicate_id, std::move(eval_fn)).second) {
            throw std::runtime_error("GuardRegistry duplicate predicate id");
        }
    }

    template <class Policy>
    bool eval(
        const Policy& memory_policy,
        v2::GuardPredicateId predicate_id,
        v2::FieldSlot field_slot,
        const CurrentHandle& current_handle,
        const std::string& argument) const {
        const auto it = eval_by_id_.find(predicate_id);
        if (it == eval_by_id_.end()) {
            throw std::runtime_error("GuardRegistry unknown predicate id");
        }

        const EqualFn equal_fn = [&memory_policy](v2::FieldSlot slot, const CurrentHandle& lhs, const CurrentHandle& rhs) {
            return memory_policy.equal(slot, lhs, rhs);
        };
        const ArgumentFactoryFn argument_factory = [](const std::string& token) {
            return Policy::from_debug_string(token);
        };

        return it->second(field_slot, current_handle, argument, equal_fn, argument_factory);
    }

private:
    std::unordered_map<v2::GuardPredicateId, EvalFn> eval_by_id_;
};

inline GuardRegistry make_builtin_guard_registry() {
    GuardRegistry registry;
    registry.register_predicate(
        kGuardPredicateEquals,
        [](v2::FieldSlot field_slot,
           const GuardRegistry::CurrentHandle& current_handle,
           const std::string& argument,
           const GuardRegistry::EqualFn& equal_fn,
           const GuardRegistry::ArgumentFactoryFn& argument_factory) {
            const auto argument_handle = argument_factory(argument);
            return equal_fn(field_slot, current_handle, argument_handle);
        });
    return registry;
}

class Executor final {
public:
    template <class StorageT, class Policy = DefaultMemoryPolicy>
    void run(
        const v2::ExecutionPlan& plan,
        const GraphSchema& schema,
        StorageT& storage,
        const Policy& memory_policy,
        const RuntimeOperationRegistry& operations,
        const GuardRegistry& guards,
        std::vector<std::string>* trace = nullptr,
        std::vector<std::string>* executed = nullptr,
        std::vector<std::string>* skipped_guard = nullptr,
        const std::function<void(v2::NodeSlot)>* on_guard_skip = nullptr,
        const std::function<void(v2::NodeSlot)>* on_execute_start = nullptr,
        const std::function<void(v2::NodeSlot, const Commit&)>* on_commit_applied = nullptr) const {
        if (!storage.is_dag_open()) {
            throw std::runtime_error("Executor::run requires begin_run() to open a DAG run first");
        }
        if (plan.dirty_inputs.bit_count() != schema.field_count()) {
            throw std::runtime_error("Executor::run dirty_inputs mask shape mismatch");
        }
        if (plan.requested_outputs.bit_count() != schema.field_count()) {
            throw std::runtime_error("Executor::run requested_outputs mask shape mismatch");
        }
        if (plan.active_nodes.bit_count() != schema.node_count()) {
            throw std::runtime_error("Executor::run active_nodes mask shape mismatch");
        }

        struct NodeReadBinding final {
            NodeReadBinding(const StorageT& storage_in, const GraphSchema& schema_in, const Policy& memory_policy_in) noexcept
                : storage(&storage_in), schema(&schema_in), memory_policy(&memory_policy_in) {}

            Commit::Handle get_handle(v2::FieldSlot field_slot) const {
                return storage->read_node_handle(field_slot, *schema);
            }

            std::string_view field_name(v2::FieldSlot field_slot) const {
                return schema->field_name(field_slot);
            }

            std::string debug_string(v2::FieldSlot field_slot, const Commit::Handle& handle) const {
                return memory_policy->debug_string(field_slot, handle);
            }

            const StorageT* storage = nullptr;
            const GraphSchema* schema = nullptr;
            const Policy* memory_policy = nullptr;
        };

        const NodeReadBinding binding(storage, schema, memory_policy);
        const RuntimeOperationRegistry::ReadHandleFn read_handle = [&binding](v2::FieldSlot field_slot) {
            return binding.get_handle(field_slot);
        };
        const RuntimeOperationRegistry::FieldNameFn field_name = [&binding](v2::FieldSlot field_slot) {
            return binding.field_name(field_slot);
        };
        const RuntimeOperationRegistry::DebugStringFn debug_string = [&binding](v2::FieldSlot field_slot, const Commit::Handle& handle) {
            return binding.debug_string(field_slot, handle);
        };

        for (const auto node_slot : plan.topo) {
            if (!plan.active_nodes.test(node_slot)) {
                throw std::runtime_error("Executor::run topo contains a node outside active_nodes");
            }

            const auto op_id = schema.op_of(node_slot);
            if (!operations.contains(op_id)) {
                throw std::runtime_error(
                    "Executor::run operation registry mismatch for node '" + std::string(schema.node_name(node_slot)) + "'");
            }

            const auto& guard = schema.guard_of(node_slot);
            if (guard) {
                const auto current_handle = binding.get_handle(guard->field);
                const bool guard_ok = guards.eval(memory_policy, guard->predicate, guard->field, current_handle, guard->argument);
                if (!guard_ok) {
                    if (on_guard_skip && *on_guard_skip) {
                        (*on_guard_skip)(node_slot);
                    }
                    if (skipped_guard) skipped_guard->push_back(std::string(schema.node_name(node_slot)));
                    if (trace) trace->push_back("skip_guard:" + std::string(schema.node_name(node_slot)));
                    continue;
                }
            }

            if (on_execute_start && *on_execute_start) {
                (*on_execute_start)(node_slot);
            }

            const auto commit = operations.execute(op_id, node_slot, read_handle, field_name, debug_string).resolved(schema);
            storage.apply_node_commit(commit, memory_policy, &schema);

            if (on_commit_applied && *on_commit_applied) {
                (*on_commit_applied)(node_slot, commit);
            }

            if (executed) executed->push_back(std::string(schema.node_name(node_slot)));
            if (trace) trace->push_back("exec:" + std::string(schema.node_name(node_slot)));
        }
    }
};

} // namespace proc
