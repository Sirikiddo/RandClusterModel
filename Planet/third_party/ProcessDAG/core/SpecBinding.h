#pragma once

#include "BuiltinRegistries.h"
#include "GraphSchemaCompile.h"

#include <string>

namespace proc {

class GraphSchemaReader final {
public:
    static GraphSchema read_file(
        const std::string& path,
        const OperationRegistry& ops,
        const AlgebraRegistry& algebras);
};

inline GraphSchema read_spec_schema(const std::string& path) {
    return GraphSchemaReader::read_file(path, make_builtin_operation_registry(), make_builtin_algebra_registry());
}

} // namespace proc
