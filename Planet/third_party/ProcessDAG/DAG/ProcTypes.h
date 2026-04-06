#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace proc {

using Field = std::string;
using Str = std::string;

using KV = std::unordered_map<Field, Str>;
using FieldSet = std::unordered_set<Field>;

} // namespace proc
