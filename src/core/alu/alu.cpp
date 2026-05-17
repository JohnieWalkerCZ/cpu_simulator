#include "alu.hpp"
#include <stdexcept>

ALU::ALU(const Config &config) : data_width_(config.data_width) {
    if (data_width_ >= 64) {
        mask_ = UINT64_MAX;
        sign_bit_ = 1ULL << 63;
    } else {
        mask_ = (1ULL << data_width_) - 1;
        sign_bit_ = 1ULL << (data_width_ - 1);
    }

    for (const auto &op_def : config.alu_ops) {
        OpImpl impl;
        impl.latency = op_def.latency;
        impl.format = op_def.format;
        impl.name = op_def.name;

        if (op_def.name == "ADD")
            impl.func = add;
        else if (op_def.name == "SUB")
            impl.func = sub;
        else if (op_def.name == "MUL")
            impl.func = mul;
        else if (op_def.name == "DIV")
            impl.func = div_u;
        else if (op_def.name == "AND")
            impl.func = and_op;
        else if (op_def.name == "OR")
            impl.func = or_op;
        else if (op_def.name == "XOR")
            impl.func = xor_op;
        else if (op_def.name == "NOT")
            impl.func = not_op;
        else if (op_def.name == "SHL")
            impl.func = shl;
        else if (op_def.name == "SHR")
            impl.func = shr;
        else {
            impl.func = add;
        }

        ops_[op_def.code] = impl;
        name_to_code_[op_def.name] = op_def.code;
    }
}

ALUResult ALU::execute(const std::string &op_name, uint64_t a, uint64_t b) {
    auto it = name_to_code_.find(op_name);
    if (it == name_to_code_.end()) {
        throw std::runtime_error("Unknown ALU operation: " + op_name);
    }
    return execute(it->second, a, b);
}

ALUResult ALU::execute(uint8_t opcode, uint64_t a, uint64_t b) {
    auto it = ops_.find(opcode);
    if (it == ops_.end()) {
        throw std::runtime_error("Unknown ALU opcode: " +
                                 std::to_string(opcode));
    }

    const auto &op = it->second;

    a &= mask_;
    b &= mask_;

    uint64_t result = op.func(a, b);
    result &= mask_;

    return make_result(result, a, b, op.name);
}

int ALU::get_latency(const std::string &op_name) const {
    auto it = name_to_code_.find(op_name);
    if (it == name_to_code_.end()) {
        return 1;
    }
    return get_latency(it->second);
}

int ALU::get_latency(uint8_t opcode) const {
    auto it = ops_.find(opcode);
    if (it == ops_.end()) {
        return 1;
    }
    return it->second.latency;
}

bool ALU::has_operation(const std::string &op_name) const {
    return name_to_code_.find(op_name) != name_to_code_.end();
}

ALUResult ALU::make_result(uint64_t value, uint64_t a, uint64_t b,
                           const std::string &op) {
    bool zero = calculate_zero(value);
    bool negative = calculate_negative(value);
    bool carry = false;
    bool overflow = false;

    if (op == "ADD") {
        carry = calculate_carry_add(a, b, value);
        overflow = calculate_overflow_add(a, b, value);
    } else if (op == "SUB") {
        carry = calculate_carry_sub(a, b, value);
        overflow = calculate_overflow_sub(a, b, value);
    }

    return ALUResult(value, zero, carry, overflow, negative);
}

bool ALU::calculate_zero(uint64_t value) const { return (value & mask_) == 0; }

bool ALU::calculate_carry_add(uint64_t a, uint64_t b, uint64_t result) const {
    uint64_t max_val = mask_;
    return (static_cast<uint64_t>(a) + static_cast<uint64_t>(b)) > max_val;
}

bool ALU::calculate_carry_sub(uint64_t a, uint64_t b, uint64_t result) const {
    return a < b;
}

bool ALU::calculate_overflow_add(uint64_t a, uint64_t b,
                                 uint64_t result) const {
    bool a_sign = (a & sign_bit_) != 0;
    bool b_sign = (b & sign_bit_) != 0;
    bool r_sign = (result & sign_bit_) != 0;
    return (a_sign == b_sign) && (a_sign != r_sign);
}

bool ALU::calculate_overflow_sub(uint64_t a, uint64_t b,
                                 uint64_t result) const {
    bool a_sign = (a & sign_bit_) != 0;
    bool b_sign = (b & sign_bit_) != 0;
    bool r_sign = (result & sign_bit_) != 0;
    return (a_sign != b_sign) && (a_sign != r_sign);
}

bool ALU::calculate_negative(uint64_t value) const {
    return (value & sign_bit_) != 0;
}

uint64_t ALU::add(uint64_t a, uint64_t b) { return a + b; }

uint64_t ALU::sub(uint64_t a, uint64_t b) { return a - b; }

uint64_t ALU::mul(uint64_t a, uint64_t b) { return a * b; }

uint64_t ALU::div_u(uint64_t a, uint64_t b) {
    if (b == 0)
        return 0;
    return a / b;
}

uint64_t ALU::and_op(uint64_t a, uint64_t b) { return a & b; }

uint64_t ALU::or_op(uint64_t a, uint64_t b) { return a | b; }

uint64_t ALU::xor_op(uint64_t a, uint64_t b) { return a ^ b; }

uint64_t ALU::not_op(uint64_t a, uint64_t b) {
    (void)b;
    return ~a;
}

uint64_t ALU::shl(uint64_t a, uint64_t b) { return a << (b & 0x3F); }

uint64_t ALU::shr(uint64_t a, uint64_t b) { return a >> (b & 0x3F); }
