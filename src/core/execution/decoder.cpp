#include "decoder.hpp"

Decoder::Decoder(const Config &config) : config_(config) {
    reg_field_width_ =
        calculate_reg_bits(static_cast<int>(config.registers.size()));

    opcode_field_width_ = (config.data_width >= 8) ? 8 : config_.data_width;

    build_layout_map();
}

void Decoder::build_layout_map() {
    for (const auto &inst : config_.instructions) {
        InstructionLayout layout;
        layout.info = inst;
        layout.total_bits = 0;

        for (size_t i = 0; i < inst.encoding.size(); ++i) {
            FieldTemplate field;
            int token = inst.encoding[i];

            if (token >= 0) {
                field.token = "literal";
                field.is_literal = true;
                field.is_register = false;
                field.literal_value = token;
                field.bits = (i == 0) ? opcode_field_width_ : 4;
            } else {
                field.is_literal = false;
                field.is_register = false;
                if (token == -1) {
                    field.token = "dest";
                    field.bits = reg_field_width_;
                    field.is_register = true;
                } else if (token == -2) {
                    field.token = "src";
                    field.bits = reg_field_width_;
                    field.is_register = true;
                } else if (token == -3) {
                    field.token = "addr_reg";
                    field.bits = reg_field_width_;
                    field.is_register = true;
                } else if (token == -4) {
                    field.token = "offset";
                    field.bits = 8;
                } else {
                    field.token = "imm";
                    field.bits = 8;
                }
            }
            layout.fields.push_back(field);
            layout.total_bits += field.bits;
        }
        layout_map_[inst.opcode] = layout;
    }
}

uint8_t Decoder::peek_opcode(uint64_t first_word) const {
    int shift = config_.data_width - opcode_field_width_;
    if (shift < 0)
        shift = 0;
    return static_cast<uint8_t>((first_word >> shift) &
                                ((1ULL << opcode_field_width_) - 1));
}

int Decoder::get_total_bits(uint8_t opcode) const {
    if (layout_map_.find(opcode) != layout_map_.end()) {
        return layout_map_.at(opcode).total_bits;
    }
    return config_.data_width;
}

DecodedInstruction Decoder::decode(uint64_t instruction_bits,
                                   int fetched_bits) const {
    DecodedInstruction result;
    result.raw_bits = instruction_bits;

    uint8_t opcode = static_cast<uint8_t>(
        extract_bits(instruction_bits, 0, opcode_field_width_, fetched_bits));

    if (layout_map_.find(opcode) == layout_map_.end()) {
        result.is_valid = false;
        result.error = "Unknown opcode: " + std::to_string((int)opcode);
        return result;
    }

    const auto &layout = layout_map_.at(opcode);
    result.name = layout.info.name;
    result.opcode = opcode;
    result.is_valid = true;

    result.length_bytes = layout.total_bits / config_.data_width;
    if (result.length_bytes == 0)
        result.length_bytes = 1;

    int current_bit = 0;
    for (const auto &field : layout.fields) {
        uint64_t val = extract_bits(instruction_bits, current_bit, field.bits,
                                    fetched_bits);

        if (field.is_register) {
            if (val >= config_.registers.size()) {
                result.is_valid = false;
                result.error = "Invalid register index " + std::to_string(val) +
                               " for " + result.name;
                return result;
            }
            result.regs[field.token] = static_cast<int>(val);
        } else if (!field.is_literal) {
            result.imms[field.token] = val;
        }
        current_bit += field.bits;
    }

    return result;
}

uint64_t Decoder::extract_bits(uint64_t val, int start_bit, int width,
                               int total_bits) const {
    int shift = total_bits - start_bit - width;
    if (shift < 0)
        return val & ((1ULL << width) - 1);

    uint64_t mask = (1ULL << width) - 1;
    return (val >> shift) & mask;
}
