#pragma once
#include "../config.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct DecodedInstruction {
    std::string name;
    uint8_t opcode;
    bool is_valid = false;
    std::string error;

    std::unordered_map<std::string, int> regs;
    std::unordered_map<std::string, uint64_t> imms;

    uint64_t raw_bits;
    int length_bytes;
};

class Decoder {
  public:
    Decoder(const Config &config);

    uint8_t peek_opcode(uint64_t first_word) const;
    int get_total_bits(uint8_t opcode) const;
    DecodedInstruction decode(uint64_t instruction_bits,
                              int fetched_bits) const;

    static int calculate_reg_bits(int reg_count) {
        if (reg_count <= 1)
            return 1;
        return static_cast<int>(std::ceil(std::log2(reg_count)));
    }

    int get_opcode_width() const { return opcode_field_width_; }

  private:
    const Config &config_;
    int reg_field_width_;
    int opcode_field_width_;

    struct FieldTemplate {
        std::string token;
        int bits;
        bool is_register;
        bool is_literal;
        int literal_value;
    };

    struct InstructionLayout {
        Instruction info;
        std::vector<FieldTemplate> fields;
        int total_bits;
    };

    std::unordered_map<uint8_t, InstructionLayout> layout_map_;

    void build_layout_map();
    uint64_t extract_bits(uint64_t val, int start_bit, int width,
                          int total_bits) const;
};
