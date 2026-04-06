#include "SpecBinding.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace proc {
namespace {

using FieldDef = GraphSchemaBuilder::FieldDef;
using GuardDef = GraphSchemaBuilder::GuardDef;
using NodeDef = GraphSchemaBuilder::NodeDef;

struct ParsedSpec final {
    std::vector<FieldDef> field_defs;
    std::vector<NodeDef> nodes;
    GraphSchema::StorageLayout roles;
};

[[noreturn]] void fail_line(int line_no, const std::string& msg) {
    throw std::runtime_error("line " + std::to_string(line_no) + ": " + msg);
}

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

bool parse_guard_eq(const std::string& expr, GuardDef& g) {
    auto p = expr.find("==");
    if (p == std::string::npos) return false;
    g.field = trim(expr.substr(0, p));
    g.equals = trim(expr.substr(p + 2));
    return !g.field.empty() && !g.equals.empty();
}

FieldDef parse_field_spec(const std::string& line, int line_no) {
    auto colon = line.find(':');
    if (colon == std::string::npos) {
        FieldDef spec;
        spec.name = trim(line);
        if (spec.name.empty()) fail_line(line_no, "empty field name in fields block");
        return spec;
    }

    FieldDef spec;
    spec.name = trim(line.substr(0, colon));
    spec.type_tag = trim(line.substr(colon + 1));
    if (spec.name.empty()) fail_line(line_no, "empty field name in fields block");
    return spec;
}

void require_known_field(const std::unordered_set<Field>& fields, int line_no, const Field& field, const std::string& what) {
    if (!fields.contains(field)) {
        fail_line(line_no, what + " references unknown field '" + field + "'");
    }
}

int disjoint_line_for_message(const std::string& msg, int inputs_line, int state_line, int outputs_line) {
    int line_no = 0;

    if (msg.find("inputs and state") != std::string::npos) {
        line_no = std::max(inputs_line, state_line);
    } else if (msg.find("state and outputs") != std::string::npos) {
        line_no = std::max(state_line, outputs_line);
    } else if (msg.find("inputs and outputs") != std::string::npos) {
        line_no = std::max(inputs_line, outputs_line);
    }

    if (line_no == 0) {
        line_no = std::max(inputs_line, std::max(state_line, outputs_line));
    }

    return line_no == 0 ? 1 : line_no;
}

ParsedSpec read_parsed_spec(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) {
        throw std::runtime_error("cannot open file: " + path);
    }

    ParsedSpec data;
    std::vector<Field> inputs;
    std::vector<Field> state;
    std::vector<Field> outputs;
    std::unordered_set<std::string> node_ids;
    std::unordered_set<Field> known_fields;

    enum class Mode { Top, InFields, InNode };
    Mode mode = Mode::Top;
    bool fields_block_seen = false;

    bool inputs_seen = false;
    bool state_seen = false;
    bool outputs_seen = false;
    int inputs_line = 0;
    int state_line = 0;
    int outputs_line = 0;

    NodeDef cur_node;
    int cur_node_start = 0;

    auto flush_node = [&](int line_no) {
        if (cur_node.id.empty()) fail_line(cur_node_start, "node missing id");
        if (cur_node.op.empty()) fail_line(cur_node_start, "node missing op");
        if (!node_ids.insert(cur_node.id).second) {
            fail_line(cur_node_start, "duplicate node id '" + cur_node.id + "'");
        }

        for (const auto& r : cur_node.reads) require_known_field(known_fields, cur_node_start, r, "reads");
        for (const auto& w : cur_node.writes) require_known_field(known_fields, cur_node_start, w, "writes");
        if (cur_node.guard) require_known_field(known_fields, cur_node_start, cur_node.guard->field, "guard");

        data.nodes.push_back(std::move(cur_node));
        cur_node = NodeDef{};
        cur_node_start = line_no;
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
            if (mode == Mode::InFields) {
                mode = Mode::Top;
                continue;
            }
            if (mode == Mode::InNode) {
                flush_node(line_no);
                mode = Mode::Top;
                continue;
            }
            fail_line(line_no, "stray '}'");
        }

        if (mode == Mode::Top) {
            if (starts_with(line, "fields")) {
                if (line.find('{') == std::string::npos) fail_line(line_no, "expected '{' after fields");
                if (fields_block_seen) fail_line(line_no, "fields block must appear once");
                fields_block_seen = true;
                mode = Mode::InFields;
                continue;
            }
            if (starts_with(line, "node")) {
                if (!fields_block_seen) fail_line(line_no, "fields block must be the first block in data.txt");
                if (line.find('{') == std::string::npos) fail_line(line_no, "expected '{' after node");
                mode = Mode::InNode;
                cur_node = NodeDef{};
                cur_node_start = line_no;
                continue;
            }

            auto parse_top_level_csv = [&](const char* key, bool& seen, int& seen_line, std::vector<Field>& target) {
                if (!fields_block_seen) fail_line(line_no, "fields block must be the first block in data.txt");

                std::string k;
                std::string v;
                if (!parse_kv(line, k, v) || k != key) {
                    fail_line(line_no, std::string("bad ") + key + " syntax, expected " + key + "=a,b,c");
                }

                if (seen) {
                    fail_line(line_no, std::string(key) + " must appear once");
                }

                seen = true;
                seen_line = line_no;
                target = split_csv(v);
                if (target.empty()) {
                    fail_line(line_no, std::string(key) + " must not be empty");
                }
                for (const auto& field : target) {
                    require_known_field(known_fields, line_no, field, key);
                }
            };

            if (starts_with(line, "inputs")) {
                parse_top_level_csv("inputs", inputs_seen, inputs_line, inputs);
                continue;
            }
            if (starts_with(line, "state")) {
                parse_top_level_csv("state", state_seen, state_line, state);
                continue;
            }
            if (starts_with(line, "outputs")) {
                parse_top_level_csv("outputs", outputs_seen, outputs_line, outputs);
                continue;
            }

            fail_line(line_no, "unexpected token: " + line);
        }

        if (mode == Mode::InFields) {
            auto spec = parse_field_spec(line, line_no);
            if (!known_fields.insert(spec.name).second) {
                fail_line(line_no, "duplicate field '" + spec.name + "'");
            }
            data.field_defs.push_back(std::move(spec));
            continue;
        }

        std::string k;
        std::string v;
        if (!parse_kv(line, k, v)) {
            fail_line(line_no, "bad node line, expected key=value");
        }

        if (k == "id") {
            cur_node.id = v;
        } else if (k == "op") {
            cur_node.op = v;
        } else if (k == "reads") {
            cur_node.reads = split_csv(v);
        } else if (k == "writes") {
            cur_node.writes = split_csv(v);
        } else if (k == "guard") {
            GuardDef g;
            if (!parse_guard_eq(v, g)) fail_line(line_no, "bad guard expression, expected a==b");
            cur_node.guard = g;
        } else {
            fail_line(line_no, "unknown node key: " + k);
        }
    }

    if (mode != Mode::Top) {
        fail_line(line_no, "unexpected EOF while inside block");
    }

    if (!fields_block_seen || known_fields.empty()) {
        throw std::runtime_error("data spec requires non-empty leading fields block");
    }
    if (!inputs_seen) {
        throw std::runtime_error("data spec requires explicit inputs=... declaration");
    }
    if (!state_seen) {
        throw std::runtime_error("data spec requires explicit state=... declaration");
    }
    if (!outputs_seen) {
        throw std::runtime_error("data spec requires explicit outputs=... declaration");
    }

    data.roles.inputs = FieldSet(inputs.begin(), inputs.end());
    data.roles.state = FieldSet(state.begin(), state.end());
    data.roles.outputs = FieldSet(outputs.begin(), outputs.end());

    try {
        data.roles.validate_disjoint_or_throw();
    } catch (const std::runtime_error& ex) {
        fail_line(disjoint_line_for_message(ex.what(), inputs_line, state_line, outputs_line), ex.what());
    }

    return data;
}

} // namespace

GraphSchema GraphSchemaReader::read_file(
    const std::string& path,
    const OperationRegistry& ops,
    const AlgebraRegistry& algebras) {
    const auto parsed = read_parsed_spec(path);
    return GraphSchemaBuilder::compile(parsed.roles, parsed.field_defs, parsed.nodes, ops, algebras);
}

} // namespace proc
