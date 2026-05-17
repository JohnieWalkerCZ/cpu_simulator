#pragma once
#include "../config.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

struct ALUResult {
    uint64_t value;
    bool zero;
    bool carry;
    bool overflow;
    bool negative;

    ALUResult()
        : value(0), zero(false), carry(false), overflow(false),
          negative(false) {}
    ALUResult(uint64_t v, bool z, bool c, bool o, bool n)
        : value(v), zero(z), carry(c), overflow(o), negative(n) {}
};

class ALU {
  public:
    ALU(const Config &config);

    ALUResult execute(const std::string &op_name, uint64_t a, uint64_t b = 0);

    ALUResult execute(uint8_t opcode, uint64_t a, uint64_t b = 0);

    int get_latency(const std::string &op_name) const;
    int get_latency(uint8_t opcode) const;

    bool has_operation(const std::string &op_name) const;

  private:
    struct OpImpl {
        std::function<uint64_t(uint64_t, uint64_t)> func;
        int latency;
        std::string format;
        std::string name;
    };

    std::unordered_map<uint8_t, OpImpl> ops_;
    std::unordered_map<std::string, uint8_t> name_to_code_;
    int data_width_;
    uint64_t mask_;
    uint64_t sign_bit_;

    ALUResult make_result(uint64_t value, uint64_t a, uint64_t b,
                          const std::string &op);

    bool calculate_zero(uint64_t value) const;
    bool calculate_carry_add(uint64_t a, uint64_t b, uint64_t result) const;
    bool calculate_carry_sub(uint64_t a, uint64_t b, uint64_t result) const;
    bool calculate_overflow_add(uint64_t a, uint64_t b, uint64_t result) const;
    bool calculate_overflow_sub(uint64_t a, uint64_t b, uint64_t result) const;
    bool calculate_negative(uint64_t value) const;

    static uint64_t add(uint64_t a, uint64_t b);
    static uint64_t sub(uint64_t a, uint64_t b);
    static uint64_t mul(uint64_t a, uint64_t b);
    static uint64_t div_u(uint64_t a, uint64_t b);
    static uint64_t and_op(uint64_t a, uint64_t b);
    static uint64_t or_op(uint64_t a, uint64_t b);
    static uint64_t xor_op(uint64_t a, uint64_t b);
    static uint64_t not_op(uint64_t a, uint64_t b);
    static uint64_t shl(uint64_t a, uint64_t b);
    static uint64_t shr(uint64_t a, uint64_t b);
};
