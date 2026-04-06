#pragma once

#include "GraphSchemaCompile.h"

#include <string>
#include <vector>

namespace proc {

inline const std::vector<std::string>& builtin_operation_names() {
    static const std::vector<std::string> names = {
        "buildPlanetModel",
        "buildTerrain",
        "buildTravelGraph",
        "derive_y",
        "emit_out",
        "emit_side",
        "move",
        "op1",
        "op2",
        "op_a",
        "op_b",
        "op_c",
        "optimizePolyCount",
        "seed_x",
        "translateAbtractPlayerPosToReal",
        "translateCameraPosToViewMat",
    };
    return names;
}

inline OperationRegistry make_builtin_operation_registry() {
    OperationRegistry registry;
    const auto& names = builtin_operation_names();
    for (std::size_t i = 0; i < names.size(); ++i) {
        registry.register_op(names[i], static_cast<v2::OpId>(i + 1));
    }
    return registry;
}

inline AlgebraRegistry make_builtin_algebra_registry() {
    static const std::vector<std::string> type_tags = {
        "bool",
        "flag",
        "graph",
        "int",
        "mat4",
        "mesh",
        "scalar",
        "scratch",
        "str",
        "vec3",
    };

    AlgebraRegistry registry;
    for (std::size_t i = 0; i < type_tags.size(); ++i) {
        registry.register_type(type_tags[i], static_cast<v2::AlgebraId>(i + 1));
    }
    return registry;
}

} // namespace proc
