#include "proc/Logging.h"

#include <algorithm>
#include <iostream>
#include <ostream>

namespace proc {
namespace {

std::vector<Field> sorted_keys(const ValueStore& values) {
    std::vector<Field> keys;
    keys.reserve(values.size());
    for (const auto& [field, _] : values) keys.push_back(field);
    std::sort(keys.begin(), keys.end());
    return keys;
}

template <class Range>
void print_lines(const Range& values, const char* empty = "<none>") {
    if (values.empty()) return void(std::cout << "  " << empty << "\n");
    for (const auto& value : values) std::cout << "  " << value << "\n";
}

template <class ValueFn>
void print_fields(const std::vector<Field>& fields, const char* title, ValueFn&& value_for) {
    std::cout << "\n" << title << ":\n";
    if (fields.empty()) return void(std::cout << "  <empty>\n");
    for (const auto& field : fields) std::cout << "  " << field << " = " << value_for(field) << "\n";
}

void print_sorted_field_set(const FieldSet& fields) {
    std::vector<Field> ordered(fields.begin(), fields.end()); std::sort(ordered.begin(), ordered.end()); print_lines(ordered);
}

void print_schema_fields(const GraphSchema& schema, const char* title, v2::FieldRole role) {
    std::vector<std::string_view> fields;
    for (std::size_t i = 0; i < schema.field_count(); ++i) {
        const auto slot = static_cast<v2::FieldSlot>(i);
        if (schema.role_of(slot) == role) fields.push_back(schema.field_name(slot));
    }
    std::cout << "\n" << title << ":\n";
    print_lines(fields);
}

template <class Store>
void print_engine_phase(
    bool did_run,
    const char* phase,
    const char* empty_message,
    const std::vector<Field>& outputs,
    const char* outputs_title,
    const Store& store,
    auto&& tail) {
    std::cout << "\n=== " << phase << " ===\n";
    if (!did_run) return void(std::cout << empty_message << "\n");
    Logger::print_outputs(store.kv, outputs, outputs_title);
    tail();
}

} // namespace

OstreamLogSink::OstreamLogSink(std::ostream& stream) : stream_(&stream) {}

void OstreamLogSink::write(const LogMessage& message) {
    (*stream_) << "[" << Logger::to_string(message.level) << "]";
    if (!message.category.empty()) {
        (*stream_) << " " << message.category;
    }
    (*stream_) << ": " << message.text;
    if (message.context && !message.context->empty()) {
        (*stream_) << " (" << *message.context << ")";
    }
    (*stream_) << "\n";
}

Logger::Logger(LogSink& sink, LogLevel min_level) : sink_(&sink), min_level_(min_level) {}

void Logger::set_level(LogLevel level) noexcept { min_level_ = level; }
LogLevel Logger::level() const noexcept { return min_level_; }
bool Logger::should_log(LogLevel level) const noexcept { return static_cast<int>(level) <= static_cast<int>(min_level_); }

void Logger::log(LogMessage message) const {
    if (!sink_) {
        throw std::runtime_error("Logger requires a bound sink");
    }
    if (!should_log(message.level)) {
        return;
    }
    sink_->write(message);
}

void Logger::log(LogLevel level, std::string category, std::string text) const {
    log(LogMessage{level, std::move(category), std::move(text), std::nullopt});
}

void Logger::log(LogLevel level, std::string category, std::string text, std::string context) const {
    log(LogMessage{level, std::move(category), std::move(text), std::move(context)});
}

std::string_view Logger::to_string(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Trace:
        return "TRACE";
    }
    return "UNKNOWN";
}

void Logger::print_lines(const std::vector<std::string>& values) { ::proc::print_lines(values); }
void Logger::print_fields_sorted(const FieldSet& fields) { print_sorted_field_set(fields); }

void Logger::print_values(const ValueStore& values, const char* title) { const auto fields = sorted_keys(values); print_fields(fields, title, [&](const Field& field) { const auto it = values.find(field); return it->second ? *it->second : std::string("<missing>"); }); }

void Logger::print_graph_schema(const GraphSchema& schema) {
    std::cout << "=== GRAPH SCHEMA ===\nFields:\n";
    for (std::size_t i = 0; i < schema.field_count(); ++i) {
        const auto slot = static_cast<v2::FieldSlot>(i);
        std::cout << "  " << schema.field_name(slot);
        if (!schema.type_tag_of(slot).empty()) std::cout << " : " << schema.type_tag_of(slot);
        std::cout << "\n";
    }
    print_schema_fields(schema, "Inputs", v2::FieldRole::Input);
    print_schema_fields(schema, "State", v2::FieldRole::State);
    print_schema_fields(schema, "Outputs", v2::FieldRole::Output);
    std::cout << "\nNodes:\n";
    for (std::size_t i = 0; i < schema.node_count(); ++i) {
        const auto slot = static_cast<v2::NodeSlot>(i);
        std::cout << "  " << schema.node_name(slot) << " op=" << schema.op_name(slot) << " reads={";
        for (std::size_t j = 0; j < schema.reads_of(slot).size(); ++j) { if (j) std::cout << ", "; std::cout << schema.field_name(schema.reads_of(slot)[j]); }
        std::cout << "} writes={";
        for (std::size_t j = 0; j < schema.writes_of(slot).size(); ++j) { if (j) std::cout << ", "; std::cout << schema.field_name(schema.writes_of(slot)[j]); }
        std::cout << "}";
        if (const auto& guard = schema.guard_of(slot)) std::cout << " guard=" << schema.field_name(guard->field) << "==" << guard->argument;
        std::cout << "\n";
    }
}

void Logger::print_plan(const DefaultDagEngine::Plan& plan) { std::cout << "\n--- PLAN ---\ndirty_input_fields:\n"; ::proc::print_lines(plan.changed_fields); std::cout << "trigger map:\n"; for (const auto& [field, readers] : plan.trigger_map) { std::cout << "  " << field << " -> "; if (readers.empty()) std::cout << "<none>"; else for (std::size_t i = 0; i < readers.size(); ++i) std::cout << (i ? ", " : "") << readers[i]; std::cout << "\n"; } std::cout << "triggered_nodes:\n"; ::proc::print_lines(plan.triggered_nodes); std::cout << "active_nodes:\n"; ::proc::print_lines(plan.active_nodes); std::cout << "topo:\n"; ::proc::print_lines(plan.topo); }
void Logger::print_commit(const Commit& commit, const char* title) { std::cout << "\n" << title << ":\n"; commit.for_each_change([&](const Commit::ChangeView& change) { std::cout << "  " << Commit::field_debug_name(change) << " = " << (change.kind() == v2::ChangeKind::Tombstone ? "DEL" : std::string(Commit::debug_view(change.payload()))) << "\n"; }); }
void Logger::print_input_state(const ValueStore& values) { print_values(values, "Staged input values"); }
void Logger::print_outputs(const ValueStore& values, const std::vector<Field>& outputs, const char* title) { print_fields(outputs, title, [&](const Field& field) { const auto value = get_value_view(values, field); return value ? std::string(*value) : std::string("<missing>"); }); }

void Logger::print_prepare(bool did_prepare, const std::vector<Field>& outputs, const DefaultDagEngine& dag, const char* title) {
    print_engine_phase(
        did_prepare,
        (std::string(title) + " / PREPARE").c_str(),
        "No prepare",
        outputs,
        "Prepared outputs",
        dag.prepared_output_store(),
        [&]() {
            std::cout << "\nPending input dirty:\n";
            print_sorted_field_set(dag.dirty_inputs());
        });
}

void Logger::print_ack(bool did_ack, const std::vector<Field>& outputs, const DefaultDagEngine& dag, const char* title) {
    print_engine_phase(
        did_ack,
        (std::string(title) + " / ACK").c_str(),
        "No ack",
        outputs,
        "Published outputs",
        dag.published_output_store(),
        [&]() { print_input_state(dag.input_snapshot()); });
}

} // namespace proc
