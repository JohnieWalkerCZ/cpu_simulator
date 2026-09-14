#include "declarative_peripheral.hpp"
#include "../cpu.hpp"
#include <cctype>
#include <stdexcept>

DeclarativePeripheral::DeclarativePeripheral(CPU &cpu, const PeripheralDef &def)
    : cpu_(cpu), def_(def) {
    internal_vars_ = def.internal_state;
    for (const auto &r : def.registers) {
        registers_[r.name] = r.initial;
    }
}

void DeclarativePeripheral::reset() {
    internal_vars_ = def_.internal_state;

    for (const auto &r : def_.registers) {
        registers_[r.name] = r.initial;
    }
}

word_t DeclarativePeripheral::get_var(const std::string &name, word_t ctx) {
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
    word_t ctx;
    void skip() {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
            p++;
    }
    bool match_single(char c) {
        skip();
        if (p < s.size() && s[p] == c &&
            (p + 1 == s.size() || s[p + 1] != c)) {
            ++p;
            return true;
        }
        return false;
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
    ExprParser(std::string str, DeclarativePeripheral *periph, word_t ctx)
        : s(str), periph(periph), ctx(ctx) {}
    word_t parse() {
        word_t value = parse_logic();
        skip();
        if (p != s.size())
            throw std::runtime_error("Invalid peripheral expression: " + s);
        return value;
    }

  private:
    word_t parse_logic() {
        word_t v = parse_rel();
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
    word_t parse_rel() {
        word_t v = parse_bit();
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
    word_t parse_bit() {
        word_t v = parse_term();
        while (true) {
            if (match("<<")) {
                word_t amount = parse_term();
                v = amount >= 128 ? 0 : v << static_cast<unsigned>(amount);
            } else if (match(">>")) {
                word_t amount = parse_term();
                v = amount >= 128 ? 0 : v >> static_cast<unsigned>(amount);
            } else if (match_single('&'))
                v &= parse_term();
            else if (match_single('|'))
                v |= parse_term();
            else if (match_single('^'))
                v ^= parse_term();
            else
                break;
        }
        return v;
    }
    word_t parse_term() {
        word_t v = parse_factor();
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
    word_t parse_factor() {
        word_t v = parse_unary();
        while (true) {
            if (match("*"))
                v *= parse_unary();
            else if (match("/")) {
                word_t d = parse_unary();
                v = d ? v / d : 0;
            } else if (match("%")) {
                word_t d = parse_unary();
                v = d ? v % d : 0;
            } else
                break;
        }
        return v;
    }
    word_t parse_unary() {
        skip();
        if (match("!"))
            return !parse_unary();
        if (match("~"))
            return ~parse_unary();
        if (match("-"))
            return -parse_unary();
        return parse_primary();
    }
    word_t parse_primary() {
        skip();
        if (match("(")) {
            word_t v = parse_logic();
            if (!match(")"))
                throw std::runtime_error("Unclosed parenthesis in peripheral expression: " + s);
            return v;
        }
        if (p >= s.size())
            throw std::runtime_error("Unexpected end of peripheral expression: " + s);
        if (std::isdigit(static_cast<unsigned char>(s[p]))) {
            size_t start = p;
            while (p < s.size() && (isalnum(s[p]) || s[p] == 'x'))
                p++;
            std::string token = s.substr(start, p - start);
            if (token.size() > 2 && (token[1] == 'b' || token[1] == 'B')) {
                return parse_word("0b" + token.substr(2));
            }
            return parse_word(token);
        }
        if (std::isalpha(static_cast<unsigned char>(s[p])) || s[p] == '_') {
            size_t start = p;
            while (p < s.size() && (isalnum(s[p]) || s[p] == '_'))
                p++;
            std::string id = s.substr(start, p - start);
            if (match("(")) {
                word_t arg = 0;
                if (!match(")")) {
                    arg = parse_logic();
                    if (!match(")"))
                        throw std::runtime_error("Unclosed function call in peripheral expression: " + s);
                }
                if (id == "sys_read")
                    return periph->cpu_.get_memory().read(arg);
                if (id == "host_pop_char") {
                    char c = 0;
                    if (periph->host_pop_)
                        c = periph->host_pop_();
                    return static_cast<word_t>(c);
                }
                return 0;
            }
            return periph->get_var(id, ctx);
        }
        throw std::runtime_error("Invalid peripheral expression: " + s);
    }
};

word_t DeclarativePeripheral::evaluate_expr(const std::string &expr,
                                            word_t context_value) {
    ExprParser parser(expr, this, context_value);
    return parser.parse();
}

void DeclarativePeripheral::execute_ast(const nlohmann::json &ast,
                                        word_t ctx) {
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
            word_t val = evaluate_expr(node.value("expr", "0"), ctx);
            if (registers_.count(target))
                registers_[target] = val;
            else if (internal_vars_.count(target))
                internal_vars_[target] = val;
        } else if (type == "call") {
            std::string func = node["func"];
            std::vector<word_t> args;
            if (node.contains("args")) {
                for (const auto &arg : node["args"])
                    args.push_back(evaluate_expr(arg, ctx));
            }

            if (func == "trigger_interrupt" && args.size() >= 1)
                cpu_.trigger_interrupt(static_cast<int>(args[0]));
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

word_t DeclarativePeripheral::read(word_t offset) {
    for (const auto &rdef : def_.registers) {
        const word_t start = static_cast<word_t>(rdef.offset);
        const word_t end = start + static_cast<word_t>(rdef.size_bytes);
        if (offset >= start && offset < end) {
        if (rdef.access.find('r') != std::string::npos) {
            word_t val = registers_[rdef.name] & mask_for_width(rdef.size_bytes * 8);
            if (!rdef.on_read.is_null())
                execute_ast(rdef.on_read, val);
            val = registers_[rdef.name] & mask_for_width(rdef.size_bytes * 8);
            if (offset != start)
                return (val >> ((offset - start) * 8)) & 0xFF;
            return val;
        }
        return 0;
    }
    }
    return 0;
}

void DeclarativePeripheral::write(word_t offset, word_t value) {
    for (const auto &rdef : def_.registers) {
        const word_t start = static_cast<word_t>(rdef.offset);
        const word_t end = start + static_cast<word_t>(rdef.size_bytes);
        if (offset >= start && offset < end) {
        if (rdef.access.find('w') != std::string::npos) {
            const word_t mask = mask_for_width(rdef.size_bytes * 8);
            if (!rdef.on_write.is_null())
                execute_ast(rdef.on_write, value & mask);
            else if (offset == start)
                registers_[rdef.name] = value & mask;
            else {
                const int shift = static_cast<int>((offset - start) * 8);
                registers_[rdef.name] = (registers_[rdef.name] & ~(static_cast<word_t>(0xFF) << shift)) |
                                        ((value & 0xFF) << shift);
            }
        }
        return;
    }
    }
}
