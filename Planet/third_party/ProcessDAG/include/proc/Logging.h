#pragma once

#include "ProcessDag.h"
#include "Schema.h"

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace proc {

enum class LogLevel {
    Error,
    Warn,
    Info,
    Debug,
    Trace,
};

struct LogMessage final {
    LogLevel level{LogLevel::Info};
    std::string category;
    std::string text;
    std::optional<std::string> context;
};

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(const LogMessage& message) = 0;
};

class OstreamLogSink final : public LogSink {
public:
    explicit OstreamLogSink(std::ostream& stream);
    void write(const LogMessage& message) override;

private:
    std::ostream* stream_ = nullptr;
};

class Logger final {
public:
    Logger(LogSink& sink, LogLevel min_level = LogLevel::Info);

    void set_level(LogLevel level) noexcept;
    [[nodiscard]] LogLevel level() const noexcept;
    [[nodiscard]] bool should_log(LogLevel level) const noexcept;
    void log(LogMessage message) const;
    void log(LogLevel level, std::string category, std::string text) const;
    void log(LogLevel level, std::string category, std::string text, std::string context) const;

    [[nodiscard]] static std::string_view to_string(LogLevel level) noexcept;

    static void print_graph_schema(const GraphSchema& schema);
    static void print_values(const ValueStore& values, const char* title);
    static void print_plan(const DefaultDagEngine::Plan& plan);
    static void print_commit(const Commit& commit, const char* title = "Commit");
    static void print_input_state(const ValueStore& values);
    static void print_outputs(const ValueStore& values, const std::vector<Field>& outputs, const char* title);
    static void print_prepare(bool did_prepare, const std::vector<Field>& outputs, const DefaultDagEngine& dag, const char* title);
    static void print_ack(bool did_ack, const std::vector<Field>& outputs, const DefaultDagEngine& dag, const char* title);
    static void print_fields_sorted(const FieldSet& fields);

private:
    static void print_lines(const std::vector<std::string>& values);

    LogSink* sink_ = nullptr;
    LogLevel min_level_{LogLevel::Info};
};

} // namespace proc
