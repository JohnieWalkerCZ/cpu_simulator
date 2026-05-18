#pragma once
#include "../config.hpp"
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

class ALU {
  public:
    struct FullResult {
        uint64_t value;
        uint64_t flags_register;
    };

    ALU(const Config &config);
    FullResult execute(const std::string &op_name, uint64_t a, uint64_t b = 0,
                       uint64_t c = 0);

  private:
    const Config &config_;
    uint64_t mask_;
    uint64_t sign_bit_;

    enum class TokenType {
        OPERAND_A,
        OPERAND_B,
        OPERAND_C,
        LITERAL,
        OP_ADD,
        OP_SUB,
        OP_MUL,
        OP_DIV,
        OP_AND,
        OP_OR,
        OP_XOR,
        OP_NOT,
        OP_SHL,
        OP_SHR
    };

    struct Token {
        TokenType type;
        uint64_t value = 0;
    };

    std::unordered_map<std::string, std::vector<Token>> compiled_ops_;

    void compile_expressions();
    std::vector<Token> tokenize(const std::string &expr);
    std::vector<Token> shunting_yard(const std::vector<Token> &tokens);
    uint64_t evaluate_rpn(const std::vector<Token> &rpn, uint64_t a, uint64_t b,
                          uint64_t c);

    bool calc_zero(uint64_t res) { return (res & mask_) == 0; }
    bool calc_negative(uint64_t res) { return (res & sign_bit_) != 0; }
    bool calc_carry_add(uint64_t a, uint64_t b) { return (a > mask_ - b); }
    bool calc_carry_sub(uint64_t a, uint64_t b) { return a < b; }
};
