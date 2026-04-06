#pragma once

#include "ProcessDag.h"
#include "Schema.h"

#include <optional>
#include <string>
#include <vector>

namespace proc {

struct ScenarioRun final {
    ValueStore init;
    std::vector<Commit> commits;
    std::optional<std::vector<Field>> outputs_override;
};

class ScenarioReader final {
public:
    static ScenarioRun read_file(const std::string& path, const GraphSchema& schema);
};

} // namespace proc
