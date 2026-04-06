#pragma once

#include "Commit.h"
#include "../core/BuiltinRegistries.h"
#include "../core/GraphSchemaCompile.h"

#include <cstddef>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace proc {

class RuntimeOperationRegistry final {
public:
    using ReadHandleFn = std::function<Commit::Handle(v2::FieldSlot)>;
    using FieldNameFn = std::function<std::string_view(v2::FieldSlot)>;
    using DebugStringFn = std::function<std::string(v2::FieldSlot, const Commit::Handle&)>;
    using ExecuteFn = std::function<Commit(const ReadHandleFn&, const FieldNameFn&, const DebugStringFn&)>;

    RuntimeOperationRegistry() = default;
    explicit RuntimeOperationRegistry(OperationRegistry compile_registry)
        : compile_registry_(std::move(compile_registry)) {}

    static RuntimeOperationRegistry make_builtin_synthetic(const GraphSchema& schema) {
        RuntimeOperationRegistry registry(make_builtin_operation_registry());
        registry.bind_synthetic_schema(schema);
        return registry;
    }

    void register_op(std::string name, v2::OpId id) { compile_registry_.register_op(std::move(name), id); }
    [[nodiscard]] std::optional<v2::OpId> find_id(std::string_view name) const { return compile_registry_.find_id(name); }
    [[nodiscard]] bool contains(v2::OpId id) const noexcept { return compile_registry_.contains(id); }

    void bind_synthetic_schema(const GraphSchema& schema) {
        for (std::size_t i = 0; i < schema.node_count(); ++i) {
            const auto node_slot = static_cast<v2::NodeSlot>(i);
            const auto op_id = schema.op_of(node_slot);
            if (!contains(op_id)) {
                continue;
            }

            const auto node_id = std::string(schema.node_name(node_slot));
            const auto reads = schema.reads_of(node_slot);
            const auto writes = schema.writes_of(node_slot);
            bind_executor(
                op_id,
                node_slot,
                // Keep executable bodies thin here. Heavy inline bodies have been unstable under
                // the MSVC module/header-unit path used by the public Proc surface.
                [node_id, reads, writes](
                    const ReadHandleFn& read_handle,
                    const FieldNameFn& field_name,
                    const DebugStringFn&) -> Commit {
                    return execute_synthetic_node(node_id, reads, writes, read_handle, field_name);
                });
        }
    }

    void bind_executor(v2::OpId id, v2::NodeSlot slot, ExecuteFn execute_fn) {
        if (!contains(id)) {
            throw std::runtime_error("RuntimeOperationRegistry cannot bind executable to unknown op id");
        }
        if (!execute_fn) {
            throw std::runtime_error("RuntimeOperationRegistry requires non-null executable binding");
        }

        auto& by_slot = execute_by_op_and_slot_[id];
        if (!by_slot.emplace(slot, std::move(execute_fn)).second) {
            throw std::runtime_error("RuntimeOperationRegistry duplicate executable binding for node slot");
        }
    }

    [[nodiscard]] bool has_binding(v2::OpId id, v2::NodeSlot slot) const noexcept {
        const auto by_op = execute_by_op_and_slot_.find(id);
        if (by_op == execute_by_op_and_slot_.end()) return false;
        return by_op->second.contains(slot);
    }

    [[nodiscard]] const ExecuteFn& executor_of(v2::OpId id, v2::NodeSlot slot) const {
        const auto by_op = execute_by_op_and_slot_.find(id);
        if (by_op == execute_by_op_and_slot_.end()) {
            throw std::runtime_error("RuntimeOperationRegistry missing executable binding for op id");
        }

        const auto by_slot = by_op->second.find(slot);
        if (by_slot == by_op->second.end()) {
            throw std::runtime_error("RuntimeOperationRegistry missing executable binding for node slot");
        }

        return by_slot->second;
    }

    Commit execute(
        v2::OpId id,
        v2::NodeSlot slot,
        const ReadHandleFn& read_handle,
        const FieldNameFn& field_name,
        const DebugStringFn& debug_string) const {
        return executor_of(id, slot)(read_handle, field_name, debug_string);
    }

private:
    static Commit execute_synthetic_node(
        const std::string& node_id,
        const std::vector<v2::FieldSlot>& reads,
        const std::vector<v2::FieldSlot>& writes,
        const ReadHandleFn& read_handle,
        const FieldNameFn& field_name) {
        Commit c;

        std::ostringstream suffix;
        bool first = true;
        for (const auto read_slot : reads) {
            if (!first) suffix << ';';
            first = false;

            auto handle = read_handle(read_slot);
            const auto value = Commit::debug_view(handle);
            const auto read_name = std::string(field_name(read_slot));
            const auto debug_value = value.empty() ? std::string("<missing>") : std::string(value);
            suffix << read_name;
            suffix << '=';
            suffix << debug_value;
        }

        const auto suffix_text = suffix.str();
        for (const auto write_slot : writes) {
            const auto write_name = std::string(field_name(write_slot));
            const auto computed_value = std::string("computed:") + node_id + "|" + suffix_text;
            c.set(
                write_slot,
                computed_value,
                v2::WriteLifetime::Persistent,
                write_name);
        }
        return c;
    }

    OperationRegistry compile_registry_;
    std::unordered_map<v2::OpId, std::unordered_map<v2::NodeSlot, ExecuteFn>> execute_by_op_and_slot_;
};

} // namespace proc
