#include "assembler.hpp"
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

OperandType Assembler::determine_operand_type(const std::string &token) {
    if (get_register_index(token) != -1)
        return OperandType::Register;
    return OperandType::Immediate;
}

std::vector<OperandType>
Assembler::get_expected_signature(const Instruction &inst) {
    std::vector<OperandType> signature;
    for (int enc : inst.encoding) {
        if (enc >= 0) {
            continue;
        } else if (enc >= -3) {
            signature.push_back(OperandType::Register);
        } else {
            signature.push_back(OperandType::Immediate);
        }
    }

    return signature;
}

Assembler::Assembler(const Config &config) : config_(config) {
    reg_field_width_ =
        Decoder::calculate_reg_bits(static_cast<int>(config.registers.size()));

    opcode_field_width_ = (config.data_width >= 8) ? 8 : config.data_width;
}

std::vector<std::string> Assembler::tokenize(const std::string &line) {
    std::vector<std::string> tokens;
    std::string current;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == ';' ||
            (c == '/' && i + 1 < line.size() && line[i + 1] == '/')) {
            break;
        }
        if (std::isspace(c) || c == ',') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty())
        tokens.push_back(current);
    return tokens;
}

int Assembler::get_register_index(const std::string &name) const {
    for (size_t i = 0; i < config_.registers.size(); ++i) {
        if (config_.registers[i].name == name)
            return static_cast<int>(i);
    }
    return -1;
}

uint64_t Assembler::parse_operand(
    const std::string &op,
    const std::unordered_map<std::string, uint32_t> &labels) {
    std::string cleaned = op;
    if (!cleaned.empty() && cleaned[0] == '#') {
        cleaned = cleaned.substr(1);
    }

    if (labels.find(cleaned) != labels.end()) {
        return labels.at(cleaned);
    }

    try {
        return std::stoull(cleaned, nullptr, 0);
    } catch (...) {
        throw std::runtime_error("Invalid operand or missing label: " + op);
    }
}

std::vector<uint8_t> Assembler::assemble(const std::string &source,
                                         uint32_t load_address) {
    std::unordered_map<std::string, uint32_t> labels;
    std::istringstream iss(source);
    std::string line;

    struct ParsedLine {
        std::vector<std::string> tokens;
        const Instruction *inst = nullptr;
        int total_bits = 0;
        int units = 0;
    };

    std::vector<ParsedLine> parsed_lines;
    uint32_t current_addr = load_address;

    // Pass 1: Resolve labels and calculate instruction sizes
    while (std::getline(iss, line)) {
        std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty())
            continue;

        if (tokens[0].back() == ':') {
            std::string label_name = tokens[0].substr(0, tokens[0].size() - 1);
            labels[label_name] = current_addr;
            tokens.erase(tokens.begin());
            if (tokens.empty())
                continue;
        }

        std::vector<OperandType> actual_signature;
        for (size_t i = 1; i < tokens.size(); ++i) {
            actual_signature.push_back(determine_operand_type(tokens[i]));
        }

        const Instruction *matched_inst = nullptr;
        for (const auto &inst : config_.instructions) {
            if (inst.name == tokens[0]) {
                std::vector<OperandType> expected_sig =
                    get_expected_signature(inst);
                if (actual_signature == expected_sig) {
                    matched_inst = &inst;
                    break;
                }
            }
        }

        if (!matched_inst)
            throw std::runtime_error("Unknown instruction: " + tokens[0]);

        int total_bits = 0;
        for (size_t i = 0; i < matched_inst->encoding.size(); ++i) {
            int enc = matched_inst->encoding[i];
            if (enc >= 0)
                total_bits += (i == 0) ? opcode_field_width_ : 4;
            else if (enc >= -3)
                total_bits += reg_field_width_;
            else if (enc == -4 || enc == -5)
                total_bits += 8;
            else if (enc == -6)
                total_bits += 16;
            else if (enc == -7)
                total_bits += config_.addr_width;
            else
                total_bits += 8;
        }

        int units = (total_bits + config_.data_width - 1) / config_.data_width;
        parsed_lines.push_back({tokens, matched_inst, total_bits, units});
        current_addr += units;
    }

    // Pass 2: Generate machine code
    std::vector<uint8_t> output;
    for (const auto &pline : parsed_lines) {
        uint64_t accum = 0;
        size_t op_idx = 1;

        for (size_t i = 0; i < pline.inst->encoding.size(); ++i) {
            int enc = pline.inst->encoding[i];
            uint64_t val = 0;
            int width = 0;

            if (enc >= 0) {
                val = enc;
                width = (i == 0) ? opcode_field_width_ : 4;
            } else {
                if (op_idx >= pline.tokens.size())
                    throw std::runtime_error("Missing operands for " +
                                             pline.tokens[0]);
                std::string op_token = pline.tokens[op_idx++];
                if (enc >= -3) {
                    int reg = get_register_index(op_token);
                    if (reg == -1)
                        throw std::runtime_error("Unknown register: " +
                                                 op_token);
                    val = reg;
                    width = reg_field_width_;
                } else {
                    val = parse_operand(op_token, labels);
                    if (enc == -4 || enc == -5)
                        width = 8;
                    else if (enc == -6)
                        width = 16;
                    else if (enc == -7)
                        width = config_.addr_width;
                    else
                        width = 8;
                }
            }

            uint64_t mask = (width >= 64) ? ~0ULL : (1ULL << width) - 1;
            val &= mask;

            accum = (accum << width) | val;
        }

        int fetched_bits = pline.units * config_.data_width;
        int padding_bits = fetched_bits - pline.total_bits;

        accum <<= padding_bits;

        for (int shift = fetched_bits - config_.data_width; shift >= 0;
             shift -= config_.data_width) {
            uint64_t mask = (config_.data_width >= 64)
                                ? ~0ULL
                                : (1ULL << config_.data_width) - 1;
            uint8_t unit = static_cast<uint8_t>((accum >> shift) & mask);
            output.push_back(unit);
        }
    }

    return output;
}
