#include "alu.hpp"
#include <cstdint>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

ALU::ALU(const Config &config) : config_(config) {
    mask_ = (config.data_width == 64) ? ~0ULL : (1ULL << config.data_width) - 1;
    sign_bit_ = 1ULL << (config.data_width - 1);
    compile_expressions();
}

void ALU::compile_expressions() {
    for (const auto &op : config_.alu_ops) {
        auto tokens = tokenize(op.expression);
        compiled_ops_[op.name] = shunting_yard(tokens);
    }
}

std::vector<ALU::Token> ALU::tokenize(const std::string &expr) {
    std::vector<Token> tokens;
    for (size_t i = 0; i < expr.length(); ++i) {
        char ch = expr[i];
        if (isspace(ch) || ch == '(' || ch == ')') {
            if (ch == '(')
                tokens.push_back({TokenType::L_PAREN});
            if (ch == ')')
                tokens.push_back({TokenType::R_PAREN});
            continue;
        }

        if (isdigit(ch)) {
            std::string s;
            while (i < expr.length() &&
                   (isxdigit(expr[i]) || expr[i] == 'x' || expr[i] == 'X'))
                s += expr[i++];
            i--;
            tokens.push_back({TokenType::LITERAL, std::stoull(s, nullptr, 0)});
        } else if (ch == 'a' || ch == 'b' || ch == 'c') {
            if (ch == 'a')
                tokens.push_back({TokenType::OPERAND_A});
            else if (ch == 'b')
                tokens.push_back({TokenType::OPERAND_B});
            else
                tokens.push_back({TokenType::OPERAND_C});
        } else {
            // Match operators
            if (ch == '+')
                tokens.push_back({TokenType::OP_ADD});
            else if (ch == '-')
                tokens.push_back({TokenType::OP_SUB});
            else if (ch == '*')
                tokens.push_back({TokenType::OP_MUL});
            else if (ch == '/') {
                tokens.push_back({TokenType::OP_DIV});
            } else if (ch == '&')
                tokens.push_back({TokenType::OP_AND});
            else if (ch == '|')
                tokens.push_back({TokenType::OP_OR});
            else if (ch == '^')
                tokens.push_back({TokenType::OP_XOR});
            else if (ch == '~')
                tokens.push_back({TokenType::OP_NOT});
            else if (ch == '!')
                tokens.push_back({TokenType::OP_LNOT});
            else if (ch == '<' && expr[i + 1] == '<') {
                tokens.push_back({TokenType::OP_SHL});
                i++;
            } else if (ch == '>' && expr[i + 1] == '>') {
                tokens.push_back({TokenType::OP_SHR});
                i++;
            }
        }
    }
    return tokens;
}

// Shunting-Yard
std::vector<ALU::Token> ALU::shunting_yard(const std::vector<Token> &tokens) {
    std::vector<Token> output;
    std::stack<Token> ops;

    auto precedence = [](TokenType t) {
        if (t == TokenType::OP_NOT || t == TokenType::OP_LNOT)
            return 4;
        if (t == TokenType::OP_MUL || t == TokenType::OP_DIV)
            return 3;
        if (t == TokenType::OP_ADD || t == TokenType::OP_SUB)
            return 2;
        if (t == TokenType::OP_AND || t == TokenType::OP_OR ||
            t == TokenType::OP_XOR)
            return 1;
        return 0;
    };

    for (const auto &t : tokens) {
        if (t.type <= TokenType::LITERAL) {
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
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

uint64_t ALU::evaluate_rpn(const std::vector<Token> &rpn, uint64_t a,
                           uint64_t b, uint64_t c) {
    std::stack<uint64_t> s;
    for (const auto &t : rpn) {
        if (t.type == TokenType::OPERAND_A)
            s.push(a);
        else if (t.type == TokenType::OPERAND_B)
            s.push(b);
        else if (t.type == TokenType::OPERAND_C)
            s.push(c);
        else if (t.type == TokenType::LITERAL)
            s.push(t.value);
        else if (t.type == TokenType::OP_NOT) {
            uint64_t v = s.top();
            s.pop();
            s.push(~v);
        } else if (t.type == TokenType::OP_LNOT) {
            uint64_t v = s.top();
            s.pop();
            s.push(!v ? 1 : 0);
        } else {
            uint64_t right = s.top();
            s.pop();
            uint64_t left = s.top();
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
                s.push(left << right);
                break;
            case TokenType::OP_SHR:
                s.push(left >> right);
                break;
            default:
                break;
            }
        }
    }
    return s.top();
}

ALU::FullResult ALU::execute(const std::string &op_name, uint64_t a, uint64_t b,
                             uint64_t c, int width) {
    const auto &rpn = compiled_ops_.at(op_name);

    const ALUOp *op_def = nullptr;
    for (const auto &op : config_.alu_ops) {
        if (op.name == op_name) {
            op_def = &op;
            break;
        }
    }

    uint64_t op_mask = mask_;
    uint64_t op_sign_bit = sign_bit_;
    if (width > 0) {
        op_mask = (width == 64) ? ~0ULL : (1ULL << width) - 1;
        op_sign_bit = 1ULL << (width - 1);
    }

    uint64_t res = evaluate_rpn(rpn, a, b, c);
    uint64_t final_res = res & op_mask;

    uint64_t masked_a = a & op_mask;
    uint64_t masked_b = b & op_mask;

    uint64_t flags_out = 0;

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
            }
        }

        if (flag_val)
            flags_out |= (1ULL << bit_pos);
    }

    return {final_res, flags_out};
}

uint64_t ALU::evaluate_flag_expression(const std::string &expr, uint64_t a,
                                       uint64_t b, uint64_t c, uint64_t res,
                                       uint64_t max_val) {
    std::string processed = expr;

    auto replace_word = [&](const std::string &word, uint64_t val) {
        std::regex re("\\b" + word + "\\b");
        processed = std::regex_replace(processed, re, std::to_string(val));
    };

    replace_word("max_val", max_val);
    replace_word("res", res);
    replace_word("a", a);
    replace_word("b", b);
    replace_word("c", c);

    return evaluate_rpn(shunting_yard(tokenize(processed)), a, b, c);
}
