#pragma once

#include "../core/GraphSchema.h"
#include "CoreV2.h"
#include "ProcTypes.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace proc {

class Planner final {
public:
    struct DebugView final {
        std::vector<Field> changed_fields;
        std::vector<std::pair<Field, std::vector<std::string>>> trigger_map;
        std::vector<std::string> triggered_nodes;
        std::vector<std::string> active_nodes;
        std::vector<std::string> topo;
    };

    explicit Planner(const GraphSchema& schema)
        : schema_(schema),
          readers_of_(schema.field_count()),
          writers_of_(schema.field_count()),
          edges_(schema.node_count()),
          redges_(schema.node_count()) {
        index_schema();
        build_dependencies();
    }

    v2::ExecutionPlan build_plan(const v2::DirtyMask& dirty_inputs, const v2::OutputMask& requested_outputs) const {
        require_field_mask_shape(dirty_inputs, "dirty_inputs");
        require_field_mask_shape(requested_outputs, "requested_outputs");

        v2::ExecutionPlan result;
        result.dirty_inputs = dirty_inputs;
        result.requested_outputs = requested_outputs;
        result.active_nodes = v2::NodeMask(schema_.node_count());

        v2::NodeMask triggered(schema_.node_count());
        dirty_inputs.for_each_set_bit([this, &triggered](v2::FieldSlot field_slot) {
            for (const auto node_slot : readers_of_[static_cast<std::size_t>(field_slot)]) {
                if (!triggered.test(node_slot)) {
                    triggered.set(node_slot);
                }
            }
        });

        result.active_nodes = select_active_nodes(triggered, result.requested_outputs);
        result.topo = topo_order(result.active_nodes);
        return result;
    }

    v2::ExecutionPlan build_plan(const FieldSet& changed_fields, const std::vector<Field>& outputs) const {
        v2::DirtyMask dirty_inputs(schema_.field_count());
        v2::OutputMask requested_outputs(schema_.field_count());

        for (const auto& field : changed_fields) {
            const auto field_slot = schema_.find_field(field);
            if (field_slot) {
                dirty_inputs.set(*field_slot);
            }
        }

        for (const auto& output : outputs) {
            const auto output_slot = schema_.find_field(output);
            if (!output_slot) {
                throw std::runtime_error("Planner requested unknown output field '" + output + "'");
            }
            requested_outputs.set(*output_slot);
        }

        return build_plan(dirty_inputs, requested_outputs);
    }

    DebugView describe(const v2::ExecutionPlan& plan) const {
        DebugView debug;

        plan.dirty_inputs.for_each_set_bit([this, &debug](v2::FieldSlot field_slot) {
            debug.changed_fields.push_back(Field(schema_.field_name(field_slot)));
        });
        std::sort(debug.changed_fields.begin(), debug.changed_fields.end());

        v2::NodeMask triggered(schema_.node_count());
        for (const auto& field : debug.changed_fields) {
            const auto field_slot = schema_.find_field(field);
            if (!field_slot) {
                debug.trigger_map.push_back({field, {}});
                continue;
            }

            auto readers = readers_for_field(*field_slot);
            for (const auto& node_name : readers) {
                const auto node_slot = schema_.find_node(node_name);
                if (node_slot && !triggered.test(*node_slot)) {
                    triggered.set(*node_slot);
                }
            }
            debug.trigger_map.push_back({field, std::move(readers)});
        }

        triggered.for_each_set_bit([this, &debug](v2::NodeSlot node_slot) {
            debug.triggered_nodes.push_back(std::string(schema_.node_name(node_slot)));
        });
        std::sort(debug.triggered_nodes.begin(), debug.triggered_nodes.end());

        plan.active_nodes.for_each_set_bit([this, &debug](v2::NodeSlot node_slot) {
            debug.active_nodes.push_back(std::string(schema_.node_name(node_slot)));
        });
        std::sort(debug.active_nodes.begin(), debug.active_nodes.end());

        debug.topo.reserve(plan.topo.size());
        for (const auto node_slot : plan.topo) {
            debug.topo.push_back(std::string(schema_.node_name(node_slot)));
        }

        return debug;
    }

    const GraphSchema& schema() const noexcept {
        return schema_;
    }

    std::vector<std::string> node_names() const {
        std::vector<std::string> names;
        names.reserve(schema_.node_count());
        for (std::size_t i = 0; i < schema_.node_count(); ++i) {
            names.push_back(std::string(schema_.node_name(static_cast<v2::NodeSlot>(i))));
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    std::vector<std::pair<std::string, std::string>> dependency_edges() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (std::size_t from = 0; from < edges_.size(); ++from) {
            for (const auto to : edges_[from]) {
                out.push_back({
                    std::string(schema_.node_name(static_cast<v2::NodeSlot>(from))),
                    std::string(schema_.node_name(to)),
                });
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<std::string> readers_for_field(v2::FieldSlot field_slot) const {
        std::vector<std::string> readers;
        readers.reserve(readers_of_[static_cast<std::size_t>(field_slot)].size());
        for (const auto node_slot : readers_of_[static_cast<std::size_t>(field_slot)]) {
            readers.push_back(std::string(schema_.node_name(node_slot)));
        }
        return readers;
    }

private:
    void require_field_mask_shape(const v2::FieldMask& mask, const char* name) const {
        if (mask.bit_count() != schema_.field_count()) {
            throw std::runtime_error(std::string("Planner ") + name + " mask shape mismatch");
        }
    }

    void index_schema() {
        for (std::size_t node_index = 0; node_index < schema_.node_count(); ++node_index) {
            const auto node_slot = static_cast<v2::NodeSlot>(node_index);

            for (const auto field_slot : schema_.writes_of(node_slot)) {
                writers_of_[static_cast<std::size_t>(field_slot)].push_back(node_slot);
            }

            for (const auto field_slot : schema_.reads_of(node_slot)) {
                readers_of_[static_cast<std::size_t>(field_slot)].push_back(node_slot);
            }
        }

        const auto by_name = [this](v2::NodeSlot lhs, v2::NodeSlot rhs) {
            return schema_.node_name(lhs) < schema_.node_name(rhs);
        };

        for (auto& writers : writers_of_) {
            std::sort(writers.begin(), writers.end(), by_name);
            writers.erase(std::unique(writers.begin(), writers.end()), writers.end());
        }

        for (auto& readers : readers_of_) {
            std::sort(readers.begin(), readers.end(), by_name);
            readers.erase(std::unique(readers.begin(), readers.end()), readers.end());
        }
    }

    void build_dependencies() {
        for (std::size_t field_index = 0; field_index < readers_of_.size(); ++field_index) {
            const auto& readers = readers_of_[field_index];
            const auto& writers = writers_of_[field_index];
            for (const auto writer : writers) {
                for (const auto reader : readers) {
                    if (writer == reader) continue;
                    edges_[static_cast<std::size_t>(writer)].push_back(reader);
                    redges_[static_cast<std::size_t>(reader)].push_back(writer);
                }
            }
        }

        const auto by_name = [this](v2::NodeSlot lhs, v2::NodeSlot rhs) {
            return schema_.node_name(lhs) < schema_.node_name(rhs);
        };

        for (auto& edges : edges_) {
            std::sort(edges.begin(), edges.end(), by_name);
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        }

        for (auto& redges : redges_) {
            std::sort(redges.begin(), redges.end(), by_name);
            redges.erase(std::unique(redges.begin(), redges.end()), redges.end());
        }
    }

    v2::NodeMask closure_forward(const v2::NodeMask& seeds) const {
        v2::NodeMask active = seeds;
        std::vector<v2::NodeSlot> queue;
        seeds.for_each_set_bit([&queue](v2::NodeSlot node_slot) {
            queue.push_back(node_slot);
        });

        for (std::size_t i = 0; i < queue.size(); ++i) {
            for (const auto next : edges_[static_cast<std::size_t>(queue[i])]) {
                if (active.test(next)) continue;
                active.set(next);
                queue.push_back(next);
            }
        }

        return active;
    }

    void close_reverse_in_place(v2::NodeMask& active) const {
        std::vector<v2::NodeSlot> queue;
        active.for_each_set_bit([&queue](v2::NodeSlot node_slot) {
            queue.push_back(node_slot);
        });

        for (std::size_t i = 0; i < queue.size(); ++i) {
            for (const auto prev : redges_[static_cast<std::size_t>(queue[i])]) {
                if (active.test(prev)) continue;
                active.set(prev);
                queue.push_back(prev);
            }
        }
    }

    v2::NodeMask nodes_relevant_to_outputs(const v2::OutputMask& outputs) const {
        v2::NodeMask relevant(schema_.node_count());
        outputs.for_each_set_bit([this, &relevant](v2::FieldSlot field_slot) {
            for (const auto writer : writers_of_[static_cast<std::size_t>(field_slot)]) {
                if (!relevant.test(writer)) {
                    relevant.set(writer);
                }
            }
        });
        close_reverse_in_place(relevant);
        return relevant;
    }

    v2::NodeMask select_active_nodes(const v2::NodeMask& triggered, const v2::OutputMask& outputs) const {
        auto active = closure_forward(triggered);

        if (!outputs.empty()) {
            const auto relevant = nodes_relevant_to_outputs(outputs);
            active.intersect_with(relevant);
        }

        close_reverse_in_place(active);
        return active;
    }

    std::vector<v2::NodeSlot> topo_order(const v2::NodeMask& active) const {
        std::vector<int> indegree(schema_.node_count(), -1);
        active.for_each_set_bit([&indegree](v2::NodeSlot node_slot) {
            indegree[static_cast<std::size_t>(node_slot)] = 0;
        });

        active.for_each_set_bit([this, &active, &indegree](v2::NodeSlot node_slot) {
            for (const auto next : edges_[static_cast<std::size_t>(node_slot)]) {
                if (active.test(next)) {
                    indegree[static_cast<std::size_t>(next)] += 1;
                }
            }
        });

        std::vector<v2::NodeSlot> ready;
        ready.reserve(active.count());
        active.for_each_set_bit([&ready, &indegree](v2::NodeSlot node_slot) {
            if (indegree[static_cast<std::size_t>(node_slot)] == 0) {
                ready.push_back(node_slot);
            }
        });

        const auto by_name = [this](v2::NodeSlot lhs, v2::NodeSlot rhs) {
            return schema_.node_name(lhs) < schema_.node_name(rhs);
        };
        std::sort(ready.begin(), ready.end(), by_name);

        std::vector<v2::NodeSlot> topo;
        topo.reserve(active.count());

        while (!ready.empty()) {
            const auto node_slot = ready.front();
            ready.erase(ready.begin());
            topo.push_back(node_slot);

            for (const auto next : edges_[static_cast<std::size_t>(node_slot)]) {
                if (!active.test(next)) continue;
                auto& degree = indegree[static_cast<std::size_t>(next)];
                if (--degree == 0) {
                    ready.push_back(next);
                    std::sort(ready.begin(), ready.end(), by_name);
                }
            }
        }

        if (topo.size() != active.count()) {
            throw std::runtime_error("Cycle detected in active subgraph");
        }

        return topo;
    }

    const GraphSchema& schema_;
    std::vector<std::vector<v2::NodeSlot>> readers_of_;
    std::vector<std::vector<v2::NodeSlot>> writers_of_;
    std::vector<std::vector<v2::NodeSlot>> edges_;
    std::vector<std::vector<v2::NodeSlot>> redges_;
};

} // namespace proc
