#include "alu.hpp"
#include <cstdint>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

ALU::ALU(const Config &config) : config_(config) {
    mask_ = mask_for_width(config.data_width);
    sign_bit_ = static_cast<word_t>(1) << (config.data_width - 1);
    compile_expressions();
}

void ALU::compile_expressions() {
    for (const auto &op : config_.alu_ops) {
        if (!op.program.empty()) {
            std::string src;
            for (const auto &line : op.program) {
                src += line;
                src += '\n';
            }
            try {
                compiled_programs_[op.name] =
                    parse_program(tokenize_program(src));
            } catch (const std::exception &e) {
                throw std::runtime_error("Invalid ALU program for operation '" +
                                         op.name + "': " + e.what());
            }
        } else {
            compiled_ops_[op.name] = shunting_yard(tokenize(op.expression));
        }
    }
}

std::vector<ALU::Token> ALU::tokenize(const std::string &expr) {
    std::vector<Token> tokens;
    // Note: while-loop (not for) because match_operator_token() advances i
    // itself; a for-loop's ++i would skip a character.
    size_t i = 0;
    while (i < expr.length()) {
        char ch = expr[i];
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == '(' || ch == ')') {
            if (ch == '(')
                tokens.push_back({TokenType::L_PAREN});
            if (ch == ')')
                tokens.push_back({TokenType::R_PAREN});
            ++i;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(ch))) {
            std::string s;
            while (i < expr.length() &&
                   (std::isxdigit(static_cast<unsigned char>(expr[i])) ||
                    expr[i] == 'x' || expr[i] == 'X'))
                s += expr[i++];
            tokens.push_back({TokenType::LITERAL, parse_word(s)});
            continue;
        }
        if (ch == 'a' || ch == 'b' || ch == 'c') {
            if (ch == 'a')
                tokens.push_back({TokenType::OPERAND_A});
            else if (ch == 'b')
                tokens.push_back({TokenType::OPERAND_B});
            else
                tokens.push_back({TokenType::OPERAND_C});
            ++i;
            continue;
        }
        // Match operators (shared with the program tokenizer, so plain
        // expressions also support ==, !=, <, <=, >, >=, &&, ||)
        if (!match_operator_token(expr, i, tokens))
            throw std::runtime_error("Invalid ALU expression character");
    }
    return tokens;
}

// Matches an operator starting at src[i] (two-character forms first),
// appends the token and advances i past it. Returns false if src[i] does
// not start an operator. Shared by the expression and program tokenizers.
bool ALU::match_operator_token(const std::string &src, size_t &i,
                               std::vector<Token> &toks) {
    char ch = src[i];
    char nx = (i + 1 < src.size()) ? src[i + 1] : '\0';
    auto push = [&](TokenType tt, size_t adv) {
        toks.push_back({tt});
        i += adv;
    };
    switch (ch) {
    case '+':
        push(TokenType::OP_ADD, 1);
        return true;
    case '-':
        push(TokenType::OP_SUB, 1);
        return true;
    case '*':
        push(TokenType::OP_MUL, 1);
        return true;
    case '/':
        push(TokenType::OP_DIV, 1);
        return true;
    case '^':
        push(TokenType::OP_XOR, 1);
        return true;
    case '~':
        push(TokenType::OP_NOT, 1);
        return true;
    case '=':
        if (nx == '=') {
            push(TokenType::OP_EQ, 2);
            return true;
        }
        return false;
    case '!':
        if (nx == '=') {
            push(TokenType::OP_NE, 2);
            return true;
        }
        push(TokenType::OP_LNOT, 1);
        return true;
    case '<':
        if (nx == '<') {
            push(TokenType::OP_SHL, 2);
            return true;
        }
        if (nx == '=') {
            push(TokenType::OP_LE, 2);
            return true;
        }
        push(TokenType::OP_LT, 1);
        return true;
    case '>':
        if (nx == '>') {
            push(TokenType::OP_SHR, 2);
            return true;
        }
        if (nx == '=') {
            push(TokenType::OP_GE, 2);
            return true;
        }
        push(TokenType::OP_GT, 1);
        return true;
    case '&':
        if (nx == '&') {
            push(TokenType::OP_LAND, 2);
            return true;
        }
        push(TokenType::OP_AND, 1);
        return true;
    case '|':
        if (nx == '|') {
            push(TokenType::OP_LOR, 2);
            return true;
        }
        push(TokenType::OP_OR, 1);
        return true;
    default:
        return false;
    }
}

// Shunting-Yard
std::vector<ALU::Token> ALU::shunting_yard(const std::vector<Token> &tokens) {
    std::vector<Token> output;
    std::stack<Token> ops;

    // Relative order of the original operators is preserved; shifts were
    // previously loosest (a likely bug) and are now C-like, comparisons and
    // logical operators slot in below them.
    auto precedence = [](TokenType t) {
        if (t == TokenType::OP_NOT || t == TokenType::OP_LNOT)
            return 7;
        if (t == TokenType::OP_MUL || t == TokenType::OP_DIV)
            return 6;
        if (t == TokenType::OP_ADD || t == TokenType::OP_SUB)
            return 5;
        if (t == TokenType::OP_SHL || t == TokenType::OP_SHR)
            return 4;
        if (t == TokenType::OP_LT || t == TokenType::OP_LE ||
            t == TokenType::OP_GT || t == TokenType::OP_GE)
            return 3;
        if (t == TokenType::OP_EQ || t == TokenType::OP_NE)
            return 2;
        if (t == TokenType::OP_AND || t == TokenType::OP_OR ||
            t == TokenType::OP_XOR)
            return 1;
        if (t == TokenType::OP_LAND || t == TokenType::OP_LOR)
            return 0;
        return -1;
    };

    for (const auto &t : tokens) {
        if (t.type <= TokenType::IDENT) {
            output.push_back(t);
        } else if (t.type == TokenType::L_PAREN) {
            ops.push(t);
        } else if (t.type == TokenType::R_PAREN) {
            while (!ops.empty() && ops.top().type != TokenType::L_PAREN) {
                output.push_back(ops.top());
                ops.pop();
            }
            if (!ops.empty())
                ops.pop();
        } else {
            while (!ops.empty() && ops.top().type != TokenType::L_PAREN &&
                   precedence(ops.top().type) >= precedence(t.type)) {
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(t);
        }
    }
    while (!ops.empty()) {
        if (ops.top().type == TokenType::L_PAREN)
            throw std::runtime_error("Unclosed parenthesis in ALU expression");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

bool ALU::is_valid_expression(const std::string &expression) {
    try {
        auto rpn = shunting_yard(tokenize(expression));
        int values = 0;
        for (const auto &token : rpn) {
            if (token.type <= TokenType::IDENT)
                ++values;
            else if (token.type == TokenType::OP_NOT ||
                     token.type == TokenType::OP_LNOT) {
                if (values < 1)
                    return false;
            } else {
                if (values < 2)
                    return false;
                --values;
            }
        }
        return values == 1;
    } catch (...) {
        return false;
    }
}

bool ALU::is_valid_program(const std::vector<std::string> &lines) {
    try {
        std::string src;
        for (const auto &l : lines) {
            src += l;
            src += '\n';
        }
        parse_program(tokenize_program(src));
        return true;
    } catch (...) {
        return false;
    }
}

word_t ALU::eval_rpn_tokens(
    const std::vector<Token> &rpn, word_t a, word_t b, word_t c,
    const std::unordered_map<std::string, word_t> *vars) {
    std::stack<word_t> s;
    auto need = [&](size_t n) {
        if (s.size() < n)
            throw std::runtime_error("Malformed ALU expression");
    };
    for (const auto &t : rpn) {
        switch (t.type) {
        case TokenType::OPERAND_A:
            s.push(a);
            break;
        case TokenType::OPERAND_B:
            s.push(b);
            break;
        case TokenType::OPERAND_C:
            s.push(c);
            break;
        case TokenType::LITERAL:
            s.push(t.value);
            break;
        case TokenType::IDENT: {
            if (!vars)
                throw std::runtime_error(
                    "Variables are only allowed in ALU programs");
            auto it = vars->find(t.name);
            if (it == vars->end())
                throw std::runtime_error("Unknown variable '" + t.name +
                                         "' in ALU program");
            s.push(it->second);
            break;
        }
        case TokenType::OP_NOT: {
            need(1);
            word_t v = s.top();
            s.pop();
            s.push(~v);
            break;
        }
        case TokenType::OP_LNOT: {
            need(1);
            word_t v = s.top();
            s.pop();
            s.push(!v ? 1 : 0);
            break;
        }
        default: {
            need(2);
            word_t right = s.top();
            s.pop();
            word_t left = s.top();
            s.pop();
            switch (t.type) {
            case TokenType::OP_ADD:
                s.push(left + right);
                break;
            case TokenType::OP_SUB:
                s.push(left - right);
                break;
            case TokenType::OP_MUL:
                s.push(left * right);
                break;
            case TokenType::OP_DIV:
                s.push(right != 0 ? left / right : 0);
                break;
            case TokenType::OP_AND:
                s.push(left & right);
                break;
            case TokenType::OP_OR:
                s.push(left | right);
                break;
            case TokenType::OP_XOR:
                s.push(left ^ right);
                break;
            case TokenType::OP_SHL:
                s.push(right >= 128 ? 0 : left << static_cast<unsigned>(right));
                break;
            case TokenType::OP_SHR:
                s.push(right >= 128 ? 0 : left >> static_cast<unsigned>(right));
                break;
            case TokenType::OP_EQ:
                s.push(left == right ? 1 : 0);
                break;
            case TokenType::OP_NE:
                s.push(left != right ? 1 : 0);
                break;
            case TokenType::OP_LT:
                s.push(left < right ? 1 : 0);
                break;
            case TokenType::OP_LE:
                s.push(left <= right ? 1 : 0);
                break;
            case TokenType::OP_GT:
                s.push(left > right ? 1 : 0);
                break;
            case TokenType::OP_GE:
                s.push(left >= right ? 1 : 0);
                break;
            case TokenType::OP_LAND:
                s.push((left != 0 && right != 0) ? 1 : 0);
                break;
            case TokenType::OP_LOR:
                s.push((left != 0 || right != 0) ? 1 : 0);
                break;
            default:
                throw std::runtime_error("Malformed ALU expression");
            }
            break;
        }
        }
    }
    if (s.size() != 1)
        throw std::runtime_error("Malformed ALU expression");
    return s.top();
}

word_t ALU::evaluate_rpn(const std::vector<Token> &rpn, word_t a, word_t b,
                         word_t c) {
    return eval_rpn_tokens(rpn, a, b, c, nullptr);
}

word_t ALU::evaluate_rpn_vars(
    const std::vector<Token> &rpn,
    const std::unordered_map<std::string, word_t> &vars) {
    return eval_rpn_tokens(rpn, 0, 0, 0, &vars);
}

// ---------------------------------------------------------------------------
// ALU mini-language ("programs")
// ---------------------------------------------------------------------------

std::vector<ALU::Token> ALU::tokenize_program(const std::string &src) {
    std::vector<Token> toks;
    size_t i = 0;
    auto peek = [&](size_t off = 0) -> char {
        return (i + off < src.size()) ? src[i + off] : '\0';
    };
    auto is_ident_start = [](char ch) {
        return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
    };
    auto is_ident_char = [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    };

    while (i < src.size()) {
        char ch = src[i];
        if (std::isspace(static_cast<unsigned char>(ch))) {
            ++i;
            continue;
        }
        // Line comments
        if (ch == '/' && peek(1) == '/') {
            while (i < src.size() && src[i] != '\n')
                ++i;
            continue;
        }
        if (ch == '(') {
            toks.push_back({TokenType::L_PAREN});
            ++i;
            continue;
        }
        if (ch == ')') {
            toks.push_back({TokenType::R_PAREN});
            ++i;
            continue;
        }
        if (ch == '{') {
            toks.push_back({TokenType::TOK_LBRACE});
            ++i;
            continue;
        }
        if (ch == '}') {
            toks.push_back({TokenType::TOK_RBRACE});
            ++i;
            continue;
        }
        if (ch == ';') {
            toks.push_back({TokenType::TOK_SEMI});
            ++i;
            continue;
        }
        if (ch == '.') {
            if (peek(1) == '.') {
                toks.push_back({TokenType::TOK_RANGE});
                i += 2;
                continue;
            }
            throw std::runtime_error(
                "Unexpected '.' in ALU program (did you mean '..'?)");
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            std::string s;
            while (i < src.size() &&
                   (std::isxdigit(static_cast<unsigned char>(src[i])) ||
                    src[i] == 'x' || src[i] == 'X'))
                s += src[i++];
            toks.push_back({TokenType::LITERAL, parse_word(s)});
            continue;
        }
        if (is_ident_start(ch)) {
            std::string w;
            while (i < src.size() && is_ident_char(src[i]))
                w += src[i++];
            TokenType kw = TokenType::IDENT;
            if (w == "if")
                kw = TokenType::KW_IF;
            else if (w == "else")
                kw = TokenType::KW_ELSE;
            else if (w == "while")
                kw = TokenType::KW_WHILE;
            else if (w == "for")
                kw = TokenType::KW_FOR;
            else if (w == "in")
                kw = TokenType::KW_IN;
            else if (w == "return")
                kw = TokenType::KW_RETURN;
            if (kw == TokenType::IDENT) {
                Token t;
                t.type = TokenType::IDENT;
                t.name = w;
                toks.push_back(t);
            } else {
                toks.push_back({kw});
            }
            continue;
        }
        if (ch == '=' && peek(1) != '=') {
            toks.push_back({TokenType::TOK_ASSIGN});
            ++i;
            continue;
        }
        // Operators (shared with the expression tokenizer; note that `//`
        // was already handled as a comment above)
        if (match_operator_token(src, i, toks))
            continue;
        throw std::runtime_error(std::string("Invalid character '") + ch +
                                 "' in ALU program");
    }
    return toks;
}

void ALU::expect_tok(const std::vector<Token> &toks, size_t &pos,
                     TokenType tt, const char *what) {
    if (pos >= toks.size() || toks[pos].type != tt)
        throw std::runtime_error(std::string("Expected ") + what +
                                 " in ALU program");
    ++pos;
}

std::vector<ALU::Token> ALU::parse_expr_until(
    const std::vector<Token> &toks, size_t &pos,
    const std::vector<TokenType> &terminators) {
    auto is_term = [&](TokenType tt) {
        for (auto t : terminators)
            if (t == tt)
                return true;
        return false;
    };
    std::vector<Token> out;
    int depth = 0;
    while (pos < toks.size()) {
        TokenType tt = toks[pos].type;
        if (tt == TokenType::L_PAREN) {
            ++depth;
        } else if (tt == TokenType::R_PAREN) {
            if (depth == 0) {
                if (!is_term(tt))
                    throw std::runtime_error(
                        "Unexpected ')' in ALU program");
                break;
            }
            --depth;
        } else if (depth == 0 && is_term(tt)) {
            break;
        }
        out.push_back(toks[pos++]);
    }
    if (out.empty())
        throw std::runtime_error("Empty expression in ALU program");
    return shunting_yard(out);
}

std::vector<ALU::ProgStmt> ALU::parse_block(const std::vector<Token> &toks,
                                            size_t &pos) {
    expect_tok(toks, pos, TokenType::TOK_LBRACE, "'{'");
    std::vector<ProgStmt> stmts;
    while (pos < toks.size() && toks[pos].type != TokenType::TOK_RBRACE)
        stmts.push_back(parse_statement(toks, pos));
    expect_tok(toks, pos, TokenType::TOK_RBRACE, "'}'");
    return stmts;
}

ALU::ProgStmt ALU::parse_statement(const std::vector<Token> &toks,
                                   size_t &pos) {
    if (pos >= toks.size())
        throw std::runtime_error("Unexpected end of ALU program");
    TokenType tt = toks[pos].type;
    ProgStmt st;

    if (tt == TokenType::IDENT) {
        st.type = ProgStmt::Type::ASSIGN;
        st.var = toks[pos].name;
        ++pos;
        expect_tok(toks, pos, TokenType::TOK_ASSIGN, "'='");
        st.expr = parse_expr_until(toks, pos, {TokenType::TOK_SEMI});
        expect_tok(toks, pos, TokenType::TOK_SEMI, "';'");
        return st;
    }
    if (tt == TokenType::KW_IF || tt == TokenType::KW_WHILE) {
        st.type = (tt == TokenType::KW_IF) ? ProgStmt::Type::IF
                                           : ProgStmt::Type::WHILE;
        ++pos;
        expect_tok(toks, pos, TokenType::L_PAREN, "'('");
        st.expr = parse_expr_until(toks, pos, {TokenType::R_PAREN});
        expect_tok(toks, pos, TokenType::R_PAREN, "')'");
        st.body = parse_block(toks, pos);
        if (st.type == ProgStmt::Type::IF && pos < toks.size() &&
            toks[pos].type == TokenType::KW_ELSE) {
            ++pos;
            st.else_body = parse_block(toks, pos);
        }
        return st;
    }
    if (tt == TokenType::KW_FOR) {
        st.type = ProgStmt::Type::FOR;
        ++pos;
        if (pos >= toks.size() || toks[pos].type != TokenType::IDENT)
            throw std::runtime_error(
                "Expected loop variable after 'for' in ALU program");
        st.var = toks[pos].name;
        ++pos;
        expect_tok(toks, pos, TokenType::KW_IN, "'in'");
        st.expr = parse_expr_until(toks, pos, {TokenType::TOK_RANGE});
        expect_tok(toks, pos, TokenType::TOK_RANGE, "'..'");
        st.expr2 = parse_expr_until(toks, pos, {TokenType::TOK_LBRACE});
        st.body = parse_block(toks, pos);
        return st;
    }
    if (tt == TokenType::KW_RETURN) {
        st.type = ProgStmt::Type::RETURN;
        ++pos;
        st.expr = parse_expr_until(toks, pos, {TokenType::TOK_SEMI});
        expect_tok(toks, pos, TokenType::TOK_SEMI, "';'");
        return st;
    }
    throw std::runtime_error("Expected statement in ALU program");
}

std::vector<ALU::ProgStmt>
ALU::parse_program(const std::vector<Token> &toks) {
    size_t pos = 0;
    std::vector<ProgStmt> stmts;
    while (pos < toks.size())
        stmts.push_back(parse_statement(toks, pos));
    return stmts;
}

word_t ALU::run_program(const std::vector<ProgStmt> &prog, word_t a, word_t b,
                        word_t c, int width) {
    std::unordered_map<std::string, word_t> vars;
    vars.reserve(16);
    vars["a"] = a;
    vars["b"] = b;
    vars["c"] = c;
    vars["width"] = static_cast<word_t>(width > 0 ? width : config_.data_width);
    int steps = 0;
    word_t result = 0;
    bool returned = false;
    run_statements(prog, vars, steps, result, returned);
    return result;
}

void ALU::run_statements(const std::vector<ProgStmt> &stmts,
                         std::unordered_map<std::string, word_t> &vars,
                         int &steps, word_t &result, bool &returned) {
    auto guard = [&]() {
        if (++steps > kMaxProgramSteps)
            throw std::runtime_error(
                "ALU program exceeded step limit (possible infinite loop)");
    };
    for (const auto &st : stmts) {
        if (returned)
            return;
        guard();
        switch (st.type) {
        case ProgStmt::Type::ASSIGN:
            vars[st.var] = evaluate_rpn_vars(st.expr, vars);
            break;
        case ProgStmt::Type::RETURN:
            result = evaluate_rpn_vars(st.expr, vars);
            returned = true;
            return;
        case ProgStmt::Type::IF:
            if (evaluate_rpn_vars(st.expr, vars) != 0)
                run_statements(st.body, vars, steps, result, returned);
            else if (!st.else_body.empty())
                run_statements(st.else_body, vars, steps, result, returned);
            break;
        case ProgStmt::Type::WHILE:
            while (evaluate_rpn_vars(st.expr, vars) != 0) {
                run_statements(st.body, vars, steps, result, returned);
                if (returned)
                    return;
                guard();
            }
            break;
        case ProgStmt::Type::FOR: {
            word_t start = evaluate_rpn_vars(st.expr, vars);
            word_t finish = evaluate_rpn_vars(st.expr2, vars);
            for (word_t i = start; i < finish; ++i) {
                vars[st.var] = i;
                run_statements(st.body, vars, steps, result, returned);
                if (returned)
                    return;
                guard();
            }
            break;
        }
        }
    }
}

ALU::FullResult ALU::execute(const std::string &op_name, word_t a, word_t b,
                             word_t c, int width) {
    const ALUOp *op_def = nullptr;
    for (const auto &op : config_.alu_ops) {
        if (op.name == op_name) {
            op_def = &op;
            break;
        }
    }
    if (!op_def)
        throw std::runtime_error("Unknown ALU operation: " + op_name);

    word_t res;
    auto pit = compiled_programs_.find(op_name);
    if (pit != compiled_programs_.end())
        res = run_program(pit->second, a, b, c, width);
    else
        res = evaluate_rpn(compiled_ops_.at(op_name), a, b, c);

    word_t op_mask = mask_;
    word_t op_sign_bit = sign_bit_;
    if (width > 0) {
        if (width > 128)
            throw std::runtime_error("Invalid ALU operand width");
        op_mask = mask_for_width(width);
        op_sign_bit = static_cast<word_t>(1) << (width - 1);
    }

    word_t final_res = res & op_mask;

    word_t masked_a = a & op_mask;
    word_t masked_b = b & op_mask;

    word_t flags_out = 0;
    word_t flags_mask = 0;

    for (const auto &[flag_name, logic_type] : op_def->flag_rules) {
        int bit_pos = -1;
        std::string expr = "";
        for (const auto &f_def : config_.alu_flags) {
            if (f_def.name == flag_name) {
                bit_pos = f_def.bit;
                expr = f_def.expression;
                break;
            }
        }
        if (bit_pos == -1)
            continue;
        flags_mask |= static_cast<word_t>(1) << bit_pos;

        bool flag_val = false;
        if (!expr.empty()) {
            flag_val = (evaluate_flag_expression(expr, a, b, c, final_res,
                                                 op_mask) != 0);
        } else {
            if (logic_type == "result_zero") {
                flag_val = (final_res == 0);
            } else if (logic_type == "result_negative") {
                flag_val = (final_res & op_sign_bit) != 0;
            } else if (logic_type == "carry_add") {
                flag_val = calc_carry_add(masked_a, masked_b, op_mask);
            } else if (logic_type == "carry_sub") {
                flag_val = calc_carry_sub(masked_a, masked_b);
            } else if (logic_type == "overflow_add") {
                flag_val = ((masked_a ^ final_res) & (masked_b ^ final_res) &
                            op_sign_bit) != 0;
            } else if (logic_type == "overflow_sub") {
                flag_val = ((masked_a ^ masked_b) & (masked_a ^ final_res) &
                            op_sign_bit) != 0;
            } else if (!logic_type.empty()) {
                flag_val = (evaluate_flag_expression(logic_type, a, b, c,
                                                     final_res, op_mask) != 0);
            }
        }

        if (flag_val)
            flags_out |= (static_cast<word_t>(1) << bit_pos);
    }

    return {final_res, flags_out, flags_mask};
}

word_t ALU::evaluate_flag_expression(const std::string &expr, word_t a,
                                     word_t b, word_t c, word_t res,
                                     word_t max_val) {
    std::string processed = expr;

    auto replace_word = [&](const std::string &word, word_t val) {
        std::regex re("\\b" + word + "\\b");
        processed = std::regex_replace(processed, re, word_to_dec_string(val));
    };

    replace_word("max_val", max_val);
    replace_word("res", res);
    replace_word("a", a);
    replace_word("b", b);
    replace_word("c", c);

    return evaluate_rpn(shunting_yard(tokenize(processed)), a, b, c);
}
