#include "declarative_peripheral.hpp"
#include "../cpu.hpp"
#include <cctype>

DeclarativePeripheral::DeclarativePeripheral(CPU &cpu, const PeripheralDef &def)
    : cpu_(cpu), def_(def) {
    internal_vars_ = def.internal_state;
    for (const auto &r : def.registers) {
        registers_[r.name] = r.initial;
        reg_map_[r.offset] = r;
    }
}

void DeclarativePeripheral::reset() {
    internal_vars_ = def_.internal_state;

    for (const auto &r : def_.registers) {
        registers_[r.name] = r.initial;
    }
}

uint64_t DeclarativePeripheral::get_var(const std::string &name, uint64_t ctx) {
    if (name == "value")
        return ctx;
    if (registers_.count(name))
        return registers_[name];
    if (internal_vars_.count(name))
        return internal_vars_[name];
    return 0;
}

class ExprParser {
    std::string s;
    size_t p = 0;
    DeclarativePeripheral *periph;
    uint64_t ctx;
    void skip() {
        while (p < s.size() && isspace(s[p]))
            p++;
    }
    bool match(const std::string &t) {
        skip();
        if (s.compare(p, t.size(), t) == 0) {
            p += t.size();
            return true;
        }
        return false;
    }

  public:
    ExprParser(std::string str, DeclarativePeripheral *periph, uint64_t ctx)
        : s(str), periph(periph), ctx(ctx) {}
    uint64_t parse() { return parse_logic(); }

  private:
    uint64_t parse_logic() {
        uint64_t v = parse_rel();
        while (true) {
            if (match("&&"))
                v = v && parse_rel();
            else if (match("||"))
                v = v || parse_rel();
            else
                break;
        }
        return v;
    }
    uint64_t parse_rel() {
        uint64_t v = parse_bit();
        while (true) {
            if (match("=="))
                v = (v == parse_bit());
            else if (match("!="))
                v = (v != parse_bit());
            else if (match(">="))
                v = (v >= parse_bit());
            else if (match("<="))
                v = (v <= parse_bit());
            else if (match(">"))
                v = (v > parse_bit());
            else if (match("<"))
                v = (v < parse_bit());
            else
                break;
        }
        return v;
    }
    uint64_t parse_bit() {
        uint64_t v = parse_term();
        while (true) {
            if (match("&"))
                v &= parse_term();
            else if (match("|"))
                v |= parse_term();
            else if (match("^"))
                v ^= parse_term();
            else if (match("<<"))
                v <<= parse_term();
            else if (match(">>"))
                v >>= parse_term();
            else
                break;
        }
        return v;
    }
    uint64_t parse_term() {
        uint64_t v = parse_factor();
        while (true) {
            if (match("+"))
                v += parse_factor();
            else if (match("-"))
                v -= parse_factor();
            else
                break;
        }
        return v;
    }
    uint64_t parse_factor() {
        uint64_t v = parse_unary();
        while (true) {
            if (match("*"))
                v *= parse_unary();
            else if (match("/")) {
                uint64_t d = parse_unary();
                v = d ? v / d : 0;
            } else
                break;
        }
        return v;
    }
    uint64_t parse_unary() {
        skip();
        if (match("!"))
            return !parse_unary();
        if (match("~"))
            return ~parse_unary();
        if (match("-"))
            return -parse_unary();
        return parse_primary();
    }
    uint64_t parse_primary() {
        skip();
        if (match("(")) {
            uint64_t v = parse();
            match(")");
            return v;
        }
        if (isdigit(s[p])) {
            size_t start = p;
            while (p < s.size() && (isalnum(s[p]) || s[p] == 'x'))
                p++;
            return std::stoull(s.substr(start, p - start), nullptr, 0);
        }
        if (isalpha(s[p]) || s[p] == '_') {
            size_t start = p;
            while (p < s.size() && (isalnum(s[p]) || s[p] == '_'))
                p++;
            std::string id = s.substr(start, p - start);
            if (match("(")) {
                uint64_t arg = 0;
                if (!match(")")) {
                    arg = parse();
                    match(")");
                }
                if (id == "sys_read")
                    return periph->cpu_.get_memory().read(arg);
                if (id == "host_pop_char") {
                    char c = 0;
                    if (periph->host_pop_)
                        c = periph->host_pop_();
                    return c;
                }
                return 0;
            }
            return periph->get_var(id, ctx);
        }
        return 0;
    }
};

uint64_t DeclarativePeripheral::evaluate_expr(const std::string &expr,
                                              uint64_t context_value) {
    ExprParser parser(expr, this, context_value);
    return parser.parse();
}

void DeclarativePeripheral::execute_ast(const nlohmann::json &ast,
                                        uint64_t ctx) {
    if (!ast.is_array())
        return;
    for (const auto &node : ast) {
        if (!node.contains("type"))
            continue;
        std::string type = node["type"];

        if (type == "if") {
            if (evaluate_expr(node.value("condition", "0"), ctx)) {
                if (node.contains("then"))
                    execute_ast(node["then"], ctx);
            } else if (node.contains("else")) {
                execute_ast(node["else"], ctx);
            }
        } else if (type == "assign") {
            std::string target = node["target"];
            uint64_t val = evaluate_expr(node.value("expr", "0"), ctx);
            if (registers_.count(target))
                registers_[target] = val;
            else if (internal_vars_.count(target))
                internal_vars_[target] = val;
        } else if (type == "call") {
            std::string func = node["func"];
            std::vector<uint64_t> args;
            if (node.contains("args")) {
                for (const auto &arg : node["args"])
                    args.push_back(evaluate_expr(arg, ctx));
            }

            if (func == "trigger_interrupt" && args.size() >= 1)
                cpu_.trigger_interrupt(args[0]);
            else if (func == "sys_write" && args.size() >= 2)
                cpu_.get_memory().write(args[0], args[1]);
            else if (func == "host_print" && args.size() >= 1) {
                if (host_print_)
                    host_print_((char)args[0]);
            }
        }
    }
}

void DeclarativePeripheral::tick() {
    if (!def_.tick_behavior.is_null()) {
        execute_ast(def_.tick_behavior, 0);
    }
}

uint64_t DeclarativePeripheral::read(uint32_t offset) {
    if (reg_map_.count(offset)) {
        const auto &rdef = reg_map_[offset];
        if (rdef.access.find('r') != std::string::npos) {
            uint64_t val = registers_[rdef.name];
            if (!rdef.on_read.is_null())
                execute_ast(rdef.on_read, val);
            return registers_[rdef.name];
        }
    }
    return 0;
}

void DeclarativePeripheral::write(uint32_t offset, uint64_t value) {
    if (reg_map_.count(offset)) {
        const auto &rdef = reg_map_[offset];
        if (rdef.access.find('w') != std::string::npos) {
            if (!rdef.on_write.is_null())
                execute_ast(rdef.on_write, value);
            else
                registers_[rdef.name] = value;
        }
    }
}
