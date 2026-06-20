#pragma once
#include "../config.hpp"
#include "../wide_int.hpp"
#include <cstdint>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

class ALU {
  public:
    struct FullResult {
        word_t value;
        word_t flags_register;
    };

    ALU(const Config &config);
    FullResult execute(const std::string &op_name, word_t a, word_t b = 0,
                       word_t c = 0, int width = 0);

  private:
    const Config &config_;
    word_t mask_;
    word_t sign_bit_;

    enum class TokenType {
        OPERAND_A,
        OPERAND_B,
        OPERAND_C,
        LITERAL,
        L_PAREN,
        R_PAREN,
        OP_ADD,
        OP_SUB,
        OP_MUL,
        OP_DIV,
        OP_AND,
        OP_OR,
        OP_XOR,
        OP_NOT,
        OP_LNOT,
        OP_SHL,
        OP_SHR
    };

    struct Token {
        TokenType type;
        word_t value = 0;
    };

    std::unordered_map<std::string, std::vector<Token>> compiled_ops_;

    void compile_expressions();
    std::vector<Token> tokenize(const std::string &expr);
    std::vector<Token> shunting_yard(const std::vector<Token> &tokens);
    word_t evaluate_rpn(const std::vector<Token> &rpn, word_t a, word_t b,
                        word_t c);

    bool calc_zero(word_t res) { return (res & mask_) == 0; }
    bool calc_negative(word_t res) { return (res & sign_bit_) != 0; }
    bool calc_carry_add(word_t a, word_t b, word_t op_mask) {
        return (a > op_mask - b);
    }
    bool calc_carry_sub(word_t a, word_t b) { return a < b; }

    word_t evaluate_flag_expression(const std::string &expr, word_t a,
                                    word_t b, word_t c, word_t res,
                                    word_t max_val);
};
