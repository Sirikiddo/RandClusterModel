#pragma once

#include "GraphSchema.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace proc {
using FieldTypeTags = std::unordered_map<Field, std::string>;

inline FieldTypeTags field_type_tags(const GraphSchema& schema) {
    FieldTypeTags tags;
    for (std::size_t i = 0; i < schema.field_count(); ++i) {
        const auto slot = static_cast<v2::FieldSlot>(i);
        tags.emplace(Field(schema.field_name(slot)), std::string(schema.type_tag_of(slot)));
    }
    return tags;
}

inline std::vector<Field> fields_with_role(const GraphSchema& schema, v2::FieldRole role) {
    std::vector<Field> fields;
    for (std::size_t i = 0; i < schema.field_count(); ++i) {
        const auto slot = static_cast<v2::FieldSlot>(i);
        if (schema.role_of(slot) == role) {
            fields.emplace_back(schema.field_name(slot));
        }
    }
    return fields;
}

class OperationRegistry final {
public:
    void register_op(std::string name, v2::OpId id) {
        if (name.empty()) {
            throw std::runtime_error("OperationRegistry requires non-empty op name");
        }

        if (!ids_.insert(id).second) {
            throw std::runtime_error("OperationRegistry duplicate op id");
        }

        if (!name_to_id_.emplace(std::move(name), id).second) {
            ids_.erase(id);
            throw std::runtime_error("OperationRegistry duplicate op name");
        }
    }

    [[nodiscard]] std::optional<v2::OpId> find_id(std::string_view name) const {
        const auto it = name_to_id_.find(std::string(name));
        if (it == name_to_id_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] bool contains(v2::OpId id) const noexcept {
        return ids_.contains(id);
    }

private:
    std::unordered_map<std::string, v2::OpId> name_to_id_;
    std::unordered_set<v2::OpId> ids_;
};

class AlgebraRegistry final {
public:
    void register_type(std::string type_tag, v2::AlgebraId id) {
        if (type_tag.empty()) {
            throw std::runtime_error("AlgebraRegistry requires non-empty type tag");
        }

        if (!ids_.insert(id).second) {
            throw std::runtime_error("AlgebraRegistry duplicate algebra id");
        }

        if (!name_to_id_.emplace(std::move(type_tag), id).second) {
            ids_.erase(id);
            throw std::runtime_error("AlgebraRegistry duplicate type tag");
        }
    }

    [[nodiscard]] std::optional<v2::AlgebraId> find_id(std::string_view type_tag) const {
        const auto it = name_to_id_.find(std::string(type_tag));
        if (it == name_to_id_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] bool contains(v2::AlgebraId id) const noexcept {
        return ids_.contains(id);
    }

private:
    std::unordered_map<std::string, v2::AlgebraId> name_to_id_;
    std::unordered_set<v2::AlgebraId> ids_;
};

class GraphSchemaBuilder final {
public:
    struct GuardDef final {
        Field field;
        std::string equals;
    };

    struct FieldDef final {
        Field name;
        std::string type_tag;
    };

    struct NodeDef final {
        std::string id;
        std::string op;
        std::vector<Field> reads;
        std::vector<Field> writes;
        std::optional<GuardDef> guard;
    };

    static GraphSchema compile(
        const GraphSchema::StorageLayout& roles,
        const std::vector<FieldDef>& field_defs,
        const std::vector<NodeDef>& nodes,
        const OperationRegistry& ops,
        const AlgebraRegistry& algebras) {
        roles.validate_disjoint_or_throw();

        GraphSchema schema;
        schema.fields_.reserve(field_defs.size());
        schema.nodes_.reserve(nodes.size());

        const auto has_field = [&](const Field& field) {
            return std::any_of(field_defs.begin(), field_defs.end(), [&](const auto& spec) {
                return spec.name == field;
            });
        };

        auto require_role_fields_known = [&](const FieldSet& fields, const char* role_name) {
            for (const auto& field : fields) {
                if (!has_field(field)) {
                    throw std::runtime_error(
                        "cannot compile schema: " + std::string(role_name) + " references unknown field '" + field + "'");
                }
            }
        };

        require_role_fields_known(roles.inputs, "inputs");
        require_role_fields_known(roles.state, "state");
        require_role_fields_known(roles.outputs, "outputs");

        for (const auto& field_def : field_defs) {
            const auto slot = static_cast<v2::FieldSlot>(schema.fields_.size());
            if (!schema.field_slots_by_name_.emplace(field_def.name, slot).second) {
                throw std::runtime_error("cannot compile schema: duplicate field '" + field_def.name + "'");
            }

            const auto algebra_id = algebras.find_id(field_def.type_tag);
            if (!algebra_id || !algebras.contains(*algebra_id)) {
                throw std::runtime_error(
                    "cannot compile schema: field '" + field_def.name + "' references unknown type tag '" + field_def.type_tag + "'");
            }

            GraphSchema::FieldRecord bound;
            bound.name = field_def.name;
            bound.slot = slot;
            bound.algebra = *algebra_id;
            bound.type_tag = field_def.type_tag;
            bound.internal_ephemeral = roles.is_internal_ephemeral(field_def.name);

            if (roles.is_input(field_def.name)) {
                bound.role = v2::FieldRole::Input;
            } else if (roles.is_output(field_def.name)) {
                bound.role = v2::FieldRole::Output;
            } else {
                bound.role = v2::FieldRole::State;
            }

            schema.fields_.push_back(std::move(bound));
        }

        std::unordered_set<std::string> node_ids;
        const auto require_field_slot = [&](const Field& field, const std::string& node_id, const char* where) {
            const auto slot = schema.find_field(field);
            if (!slot) {
                throw std::runtime_error(
                    "cannot compile schema: node '" + node_id + "' " + where + " references unknown field '" + field + "'");
            }
            return *slot;
        };

        for (const auto& node_def : nodes) {
            if (!node_ids.insert(node_def.id).second) {
                throw std::runtime_error("cannot compile schema: duplicate node id '" + node_def.id + "'");
            }

            const auto node_slot = static_cast<v2::NodeSlot>(schema.nodes_.size());
            if (!schema.node_slots_by_name_.emplace(node_def.id, node_slot).second) {
                throw std::runtime_error("cannot compile schema: duplicate node id '" + node_def.id + "'");
            }

            const auto op_id = ops.find_id(node_def.op);
            if (!op_id || !ops.contains(*op_id)) {
                throw std::runtime_error(
                    "cannot compile schema: node '" + node_def.id + "' references unknown op '" + node_def.op + "'");
            }

            GraphSchema::NodeRecord bound;
            bound.id = node_def.id;
            bound.slot = node_slot;
            bound.op = *op_id;
            bound.op_name = node_def.op;

            auto reads = node_def.reads;
            auto writes = node_def.writes;
            std::sort(reads.begin(), reads.end());
            std::sort(writes.begin(), writes.end());
            bound.reads.reserve(reads.size());
            bound.writes.reserve(writes.size());

            for (const auto& read : reads) {
                bound.reads.push_back(require_field_slot(read, node_def.id, "reads"));
            }

            for (const auto& write : writes) {
                bound.writes.push_back(require_field_slot(write, node_def.id, "writes"));
            }

            if (node_def.guard) {
                bound.guard = GraphSchema::GuardView{
                    require_field_slot(node_def.guard->field, node_def.id, "guard"),
                    kGuardPredicateEquals,
                    node_def.guard->equals,
                };
            }

            schema.nodes_.push_back(std::move(bound));
        }

        return schema;
    }
};

} // namespace proc
