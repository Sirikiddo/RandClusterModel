#pragma once

#include "GraphSchemaCompile.h"

#include <vector>

namespace proc {

std::vector<Field> output_fields(const GraphSchema& schema);

} // namespace proc
