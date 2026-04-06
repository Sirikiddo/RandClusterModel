#include "ScenarioReader.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace proc {
namespace {

[[noreturn]] void fail_line(int line_no, const std::string& msg) { throw std::runtime_error("line " + std::to_string(line_no) + ": " + msg); }

std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool starts_with(const std::string& s, const std::string& pfx) {
    return s.rfind(pfx, 0) == 0;
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(s);
    while (std::getline(ss, cur, ',')) {
        cur = trim(cur);
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

bool parse_kv(const std::string& line, std::string& k, std::string& v) {
    auto eq = line.find('=');
    if (eq == std::string::npos) return false;
    k = trim(line.substr(0, eq));
    v = trim(line.substr(eq + 1));
    return !k.empty();
}

v2::FieldSlot require_known_field(const GraphSchema& schema, int line_no, const std::string& field, const char* where) { const auto slot = schema.find_field(field); if (!slot) fail_line(line_no, std::string(where) + " references unknown field '" + field + "'"); return *slot; }

void require_role(const GraphSchema& schema, int line_no, const std::string& field, v2::FieldRole role, const char* where, const char* message) {
    const auto slot = require_known_field(schema, line_no, field, where);
    if (schema.role_of(slot) != role) fail_line(line_no, std::string(where) + message + "'" + field + "'");
}
void require_input_field(const GraphSchema& schema, int line_no, const std::string& field, const char* where) { require_role(schema, line_no, field, v2::FieldRole::Input, where, " may only set input field "); }
void require_output_field(const GraphSchema& schema, int line_no, const std::string& field) { require_role(schema, line_no, field, v2::FieldRole::Output, "outputs", " override may only request output field "); }

template <class ApplyFn>
void parse_assignment(const GraphSchema& schema, int line_no, const std::string& line, const char* where, ApplyFn&& apply) {
    std::string field;
    std::string value;
    if (!parse_kv(line, field, value)) fail_line(line_no, std::string("bad ") + where + " line: expected 'field=value'");
    require_input_field(schema, line_no, field, where);
    apply(field, value);
}

} // namespace

ScenarioRun ScenarioReader::read_file(const std::string& path, const GraphSchema& schema) {
    std::ifstream in(path);
    if (!in.good()) throw std::runtime_error("cannot open file: " + path);

    ScenarioRun run;

    enum class Mode { Top, InInit, InCommit };
    Mode mode = Mode::Top;

    Commit cur_commit;
    bool commit_open = false;

    auto flush_commit = [&]() {
        if (!commit_open) throw std::runtime_error("internal: flush_commit without open commit");
        run.commits.push_back(std::move(cur_commit));
        cur_commit = Commit{};
        commit_open = false;
    };

    std::string raw;
    int line_no = 0;

    while (std::getline(in, raw)) {
        ++line_no;

        auto hash = raw.find('#');
        if (hash != std::string::npos) raw = raw.substr(0, hash);

        std::string line = trim(raw);
        if (line.empty()) continue;

        if (line == "}") {
            if (mode == Mode::InInit) { mode = Mode::Top; continue; }
            if (mode == Mode::InCommit) { flush_commit(); mode = Mode::Top; continue; }
            fail_line(line_no, "stray '}'");
        }

        if (mode == Mode::Top) {
            if (starts_with(line, "init_snapshot")) {
                if (line.find('{') == std::string::npos) fail_line(line_no, "expected '{' after init_snapshot");
                mode = Mode::InInit;
                continue;
            }
            if (starts_with(line, "commit")) {
                if (line.find('{') == std::string::npos) fail_line(line_no, "expected '{' after commit");
                mode = Mode::InCommit;
                commit_open = true;
                cur_commit = Commit{};
                continue;
            }
            if (starts_with(line, "outputs")) {
                std::string k;
                std::string v;
                if (!parse_kv(line, k, v) || k != "outputs") fail_line(line_no, "bad outputs syntax, expected outputs=a,b,c");
                auto values = split_csv(v);
                for (const auto& field : values) require_output_field(schema, line_no, field);
                run.outputs_override = std::move(values);
                continue;
            }
            fail_line(line_no, "unexpected token: " + line);
        }

        if (mode == Mode::InInit) {
            parse_assignment(schema, line_no, line, "init_snapshot", [&](const std::string& field, const std::string& value) {
                run.init[field] = make_value(value);
            });
            continue;
        }

        if (mode == Mode::InCommit) {
            std::string k;
            std::string v;
            if (!parse_kv(line, k, v)) fail_line(line_no, "bad commit line, expected key=value");

            if (k == "its_time") {
                if (v == "true") cur_commit.its_time = true;
                else if (v == "false") cur_commit.its_time = false;
                else fail_line(line_no, "its_time must be true or false");
                continue;
            }

            require_input_field(schema, line_no, k, "commit");
            cur_commit.set(k, v);
            continue;
        }
    }

    if (mode != Mode::Top) {
        fail_line(line_no, "unexpected EOF while inside block");
    }

    return run;
}

} // namespace proc
