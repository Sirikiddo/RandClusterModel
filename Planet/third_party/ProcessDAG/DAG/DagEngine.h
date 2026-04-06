#pragma once

#include "Executor.h"
#include "Planner.h"
#include "RuntimeOperationRegistry.h"
#include "StateTypes.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace proc {

enum class FlushFailurePolicy {
    RetryNode,
    InvalidateAllInputs,
};

template <
    class Policy = DefaultMemoryPolicy,
    template <class> class BaseStoreT = BaseStore,
    template <class> class OverlayStoreT = OverlayStore,
    class CommitT = Commit>
class DagEngine final {
public:
    using MemoryPolicy = Policy;
    using CommitType = CommitT;
    using Plan = Planner::DebugView;
    using Storage = DagStorage<WorkRollbackMode::Local, Policy, BaseStoreT, OverlayStoreT, CommitT>;
    using PreparedStore = typename Storage::PreparedStore;
    using PublishedStore = typename Storage::PublishedStore;

    explicit DagEngine(GraphSchema schema, FlushFailurePolicy failure_policy = FlushFailurePolicy::InvalidateAllInputs);
    DagEngine(
        GraphSchema schema,
        RuntimeOperationRegistry operation_registry,
        GuardRegistry guard_registry,
        FlushFailurePolicy failure_policy = FlushFailurePolicy::InvalidateAllInputs);
    ~DagEngine();

    DagEngine(DagEngine&&) noexcept;
    DagEngine& operator=(DagEngine&&) noexcept;
    DagEngine(const DagEngine&) = delete;
    DagEngine& operator=(const DagEngine&) = delete;

    void init(const ValueStore& init_snapshot);
    void push_input(const CommitT& c);
    bool flush_prepare(const std::vector<Field>& outputs);
    bool ack_outputs();

    Plan plan_from_changed(const FieldSet& changed_fields) const;

    FieldSet dirty_inputs() const;
    ValueStore input_snapshot() const;
    const PreparedStore& prepared_output_store() const noexcept;
    const PublishedStore& published_output_store() const noexcept;
    bool has_prepared_outputs() const noexcept;

    void dump_graph() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

using DefaultDagEngine = DagEngine<>;

DagEngine(GraphSchema, FlushFailurePolicy) -> DagEngine<>;
DagEngine(GraphSchema, RuntimeOperationRegistry, GuardRegistry, FlushFailurePolicy) -> DagEngine<>;

} // namespace proc

#include "DagEngine.inl"
