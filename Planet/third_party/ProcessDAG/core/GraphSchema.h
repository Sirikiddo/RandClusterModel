#pragma once

#include "../DAG/CoreV2.h"
#include "../DAG/ProcTypes.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace proc {

inline constexpr v2::GuardPredicateId kGuardPredicateEquals = v2::GuardPredicateId{1};

class GraphSchemaBuilder;

class GraphSchema final {
public:
    struct StorageLayout final {
        FieldSet inputs;
        FieldSet state;
        FieldSet outputs;

        bool is_input(const Field& field) const {
            return inputs.contains(field);
        }

        bool is_state(const Field& field) const {
            return state.contains(field);
        }

        bool is_output(const Field& field) const {
            return outputs.contains(field);
        }

        bool is_state_or_output(const Field& field) const {
            return is_state(field) || is_output(field);
        }

        bool is_internal_ephemeral(const Field& field) const {
            return !is_input(field) && !is_state(field) && !is_output(field);
        }

        void validate_disjoint_or_throw() const {
            validate_pair_or_throw(inputs, state, "inputs", "state");
            validate_pair_or_throw(state, outputs, "state", "outputs");
            validate_pair_or_throw(inputs, outputs, "inputs", "outputs");
        }

    private:
        static std::optional<Field> find_first_intersection(const FieldSet& lhs, const FieldSet& rhs) {
            for (const auto& field : lhs) {
                if (rhs.contains(field)) return field;
            }
            return std::nullopt;
        }

        static void validate_pair_or_throw(
            const FieldSet& lhs,
            const FieldSet& rhs,
            const std::string& lhs_name,
            const std::string& rhs_name) {
            auto common = find_first_intersection(lhs, rhs);
            if (common) {
                throw std::runtime_error(lhs_name + " and " + rhs_name + " intersect at field '" + *common + "'");
            }
        }
    };

    struct GuardView final {
        v2::FieldSlot field{};
        v2::GuardPredicateId predicate{};
        std::string argument;
    };

    std::size_t field_count() const noexcept { return fields_.size(); }
    std::size_t node_count() const noexcept { return nodes_.size(); }
    v2::FieldRole role_of(v2::FieldSlot slot) const { return field_at(slot).role; }
    v2::AlgebraId algebra_of(v2::FieldSlot slot) const { return field_at(slot).algebra; }
    v2::OpId op_of(v2::NodeSlot slot) const { return node_at(slot).op; }
    const std::vector<v2::FieldSlot>& reads_of(v2::NodeSlot slot) const { return node_at(slot).reads; }
    const std::vector<v2::FieldSlot>& writes_of(v2::NodeSlot slot) const { return node_at(slot).writes; }
    const std::optional<GuardView>& guard_of(v2::NodeSlot slot) const { return node_at(slot).guard; }

    std::optional<v2::FieldSlot> find_field(std::string_view name) const {
        const auto it = field_slots_by_name_.find(Field(name));
        if (it == field_slots_by_name_.end()) return std::nullopt;
        return it->second;
    }

    std::optional<v2::NodeSlot> find_node(std::string_view name) const {
        const auto it = node_slots_by_name_.find(std::string(name));
        if (it == node_slots_by_name_.end()) return std::nullopt;
        return it->second;
    }

    std::string_view field_name(v2::FieldSlot slot) const { return field_at(slot).name; }
    const Field& field_key(v2::FieldSlot slot) const { return field_at(slot).name; }
    std::string_view node_name(v2::NodeSlot slot) const { return node_at(slot).id; }
    std::string_view op_name(v2::NodeSlot slot) const { return node_at(slot).op_name; }
    std::string_view type_tag_of(v2::FieldSlot slot) const { return field_at(slot).type_tag; }
    bool is_internal_ephemeral(v2::FieldSlot slot) const { return field_at(slot).internal_ephemeral; }
    StorageLayout storage_layout() const {
        StorageLayout layout;
        for (std::size_t i = 0; i < field_count(); ++i) {
            const auto slot = static_cast<v2::FieldSlot>(i);
            const Field field(field_name(slot));
            switch (role_of(slot)) {
            case v2::FieldRole::Input:
                layout.inputs.insert(field);
                break;
            case v2::FieldRole::State:
                layout.state.insert(field);
                break;
            case v2::FieldRole::Output:
                layout.outputs.insert(field);
                break;
            }
        }
        layout.validate_disjoint_or_throw();
        return layout;
    }

private:
    friend class GraphSchemaBuilder;

    struct FieldRecord final {
        Field name;
        v2::FieldSlot slot{};
        v2::FieldRole role{v2::FieldRole::State};
        v2::AlgebraId algebra{};
        std::string type_tag;
        bool internal_ephemeral = false;
    };

    struct NodeRecord final {
        std::string id;
        v2::NodeSlot slot{};
        v2::OpId op{};
        std::string op_name;
        std::vector<v2::FieldSlot> reads;
        std::vector<v2::FieldSlot> writes;
        std::optional<GuardView> guard;
    };

    const FieldRecord& field_at(v2::FieldSlot slot) const {
        const auto index = static_cast<std::size_t>(slot);
        if (index >= fields_.size()) {
            throw std::out_of_range("GraphSchema field slot out of range");
        }
        return fields_[index];
    }

    const NodeRecord& node_at(v2::NodeSlot slot) const {
        const auto index = static_cast<std::size_t>(slot);
        if (index >= nodes_.size()) {
            throw std::out_of_range("GraphSchema node slot out of range");
        }
        return nodes_[index];
    }

    std::vector<FieldRecord> fields_;
    std::vector<NodeRecord> nodes_;
    std::unordered_map<Field, v2::FieldSlot> field_slots_by_name_;
    std::unordered_map<std::string, v2::NodeSlot> node_slots_by_name_;
};

} // namespace proc
