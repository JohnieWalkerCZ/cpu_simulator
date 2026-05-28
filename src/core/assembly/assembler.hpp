#pragma once
#include "../config.hpp"
#include "../execution/decoder.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

enum class OperandType { Register, Immediate };

class Assembler {
  public:
    Assembler(const Config &config);
    std::vector<uint8_t> assemble(const std::string &source,
                                  uint32_t load_address = 0);

  private:
    const Config &config_;
    int reg_field_width_;
    int opcode_field_width_;

    std::vector<std::string> tokenize(const std::string &line);
    int get_register_index(const std::string &name) const;
    uint64_t
    parse_operand(const std::string &op,
                  const std::unordered_map<std::string, uint32_t> &labels);
    OperandType determine_operand_type(const std::string &token);
    std::vector<OperandType> get_expected_signature(const Instruction &inst);
};
