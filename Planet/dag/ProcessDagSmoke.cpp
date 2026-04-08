#include <optional>
#include <type_traits>

#if !defined(PLANET_ENABLE_DAG_BACKEND)
#define PLANET_ENABLE_DAG_BACKEND 1
#endif

#if PLANET_ENABLE_DAG_BACKEND
import Proc;
#endif

namespace {

[[maybe_unused]] bool processDagConsumerSmoke() {
#if PLANET_ENABLE_DAG_BACKEND
    proc::GraphSchema::StorageLayout roles;
    roles.inputs.insert("x");
    roles.outputs.insert("y");

    const auto schema = proc::GraphSchemaBuilder::compile(
        roles,
        {
            {"x", "str"},
            {"y", "str"},
        },
        {
            proc::GraphSchemaBuilder::NodeDef{
                "Emit",
                "emit_out",
                {"x"},
                {"y"},
                std::nullopt,
            },
        },
        proc::make_builtin_operation_registry(),
        proc::make_builtin_algebra_registry());

    proc::DefaultDagEngine dag(schema);
    proc::ValueStore init;
    init["x"] = proc::make_value("seed");
    dag.init(init);

    const auto outputs = proc::output_fields(schema);
    return dag.flush_prepare(outputs) && dag.ack_outputs();
#else
    return true;
#endif
}

#if PLANET_ENABLE_DAG_BACKEND
static_assert(std::is_default_constructible_v<proc::Commit>);
#endif

} // namespace
