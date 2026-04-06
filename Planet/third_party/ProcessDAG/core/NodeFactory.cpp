#include "NodeFactory.h"

namespace proc {

std::vector<Field> output_fields(const GraphSchema& schema) {
    return fields_with_role(schema, v2::FieldRole::Output);
}

} // namespace proc
