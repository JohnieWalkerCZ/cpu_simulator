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
        word_t flags_mask;
    };

    ALU(const Config &config);
    FullResult execute(const std::string &op_name, word_t a, word_t b = 0,
                       word_t c = 0, int width = 0);
    static bool is_valid_expression(const std::string &expression);
    static bool is_valid_program(const std::vector<std::string> &lines);

  private:
    const Config &config_;
    word_t mask_;
    word_t sign_bit_;

    enum class TokenType {
        OPERAND_A,
        OPERAND_B,
        OPERAND_C,
        LITERAL,
        IDENT,
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
        OP_SHR,
        OP_EQ,
        OP_NE,
        OP_LT,
        OP_LE,
        OP_GT,
        OP_GE,
        OP_LAND,
        OP_LOR,
        // Program-only tokens: produced by tokenize_program(), never reach
        // the RPN evaluator.
        TOK_ASSIGN,
        TOK_SEMI,
        TOK_LBRACE,
        TOK_RBRACE,
        TOK_RANGE,
        KW_IF,
        KW_ELSE,
        KW_WHILE,
        KW_FOR,
        KW_IN,
        KW_RETURN
    };

    struct Token {
        TokenType type;
        word_t value = 0;
        std::string name; // IDENT only
    };

    // Mini-language ("ALU programs"): an operation may define `program`
    // instead of `expression`. Statements: assignment, if/else, while,
    // for-in-range, return. Operands a/b/c and `width` are pre-defined
    // variables; locals are created on first assignment.
    struct ProgStmt {
        enum class Type { ASSIGN, IF, WHILE, FOR, RETURN } type;
        std::string var;              // ASSIGN target / FOR loop variable
        std::vector<Token> expr;      // RPN: rhs / condition / return value /
                                      // FOR start (inclusive)
        std::vector<Token> expr2;     // RPN: FOR end (exclusive)
        std::vector<ProgStmt> body;   // IF / WHILE / FOR
        std::vector<ProgStmt> else_body; // IF only
    };

    // Safety cap on executed statements per operation (infinite-loop guard).
    static constexpr int kMaxProgramSteps = 1000000;

    std::unordered_map<std::string, std::vector<Token>> compiled_ops_;
    std::unordered_map<std::string, std::vector<ProgStmt>> compiled_programs_;

    void compile_expressions();
    static std::vector<Token> tokenize(const std::string &expr);
    static bool match_operator_token(const std::string &src, size_t &i,
                                     std::vector<Token> &toks);
    static std::vector<Token> shunting_yard(const std::vector<Token> &tokens);
    word_t evaluate_rpn(const std::vector<Token> &rpn, word_t a, word_t b,
                        word_t c);
    static word_t
    eval_rpn_tokens(const std::vector<Token> &rpn, word_t a, word_t b,
                    word_t c,
                    const std::unordered_map<std::string, word_t> *vars);

    // Program support
    static std::vector<Token> tokenize_program(const std::string &src);
    static std::vector<ProgStmt> parse_program(const std::vector<Token> &toks);
    static ProgStmt parse_statement(const std::vector<Token> &toks,
                                    size_t &pos);
    static std::vector<ProgStmt>
    parse_block(const std::vector<Token> &toks, size_t &pos);
    static void expect_tok(const std::vector<Token> &toks, size_t &pos,
                           TokenType tt, const char *what);
    // Collect expression tokens up to (but excluding) the first terminator
    // token seen at parenthesis depth 0, then compile to RPN.
    static std::vector<Token>
    parse_expr_until(const std::vector<Token> &toks, size_t &pos,
                     const std::vector<TokenType> &terminators);
    word_t run_program(const std::vector<ProgStmt> &prog, word_t a, word_t b,
                       word_t c, int width);
    void run_statements(const std::vector<ProgStmt> &stmts,
                        std::unordered_map<std::string, word_t> &vars,
                        int &steps, word_t &result, bool &returned);
    word_t
    evaluate_rpn_vars(const std::vector<Token> &rpn,
                      const std::unordered_map<std::string, word_t> &vars);

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
