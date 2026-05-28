#include "core/execution/decoder.hpp"
#include "core/config.hpp"
#include <cassert>
#include <iostream>

int main() {
    try {
        // Load config
        auto cfg = Config::from_file("../configs/8bit.json");

        // Initialize Decoder
        Decoder decoder(cfg);

        // 1. Verify basic decoder properties
        assert(decoder.get_opcode_width() == 8);

        // 2. Peek opcode test
        // Peek opcode fetches the most significant bits of the word depending on data_width and opcode_field_width.
        // For 8-bit config, data_width = 8, opcode_field_width = 8. So shift is 0.
        assert(decoder.peek_opcode(0x05) == 5);
        assert(decoder.peek_opcode(0x0F) == 15);

        // 3. Test decoding valid instructions
        // Let's decode: ADD dest src
        // ADD opcode = 5, dest = @dest, src = @src
        // In 8bit.json encoding layout: [5, "dest", "src"]
        // opcode is 8 bits (value 5)
        // dest is 3 bits (index 0 for R0)
        // src is 3 bits (index 1 for R1)
        // Let's calculate total bits: 8 + 3 + 3 = 14 bits.
        // Number of fetched units (bytes) = (14 + 8 - 1) / 8 = 2 bytes (16 bits fetched).
        // Let's see how the 16 bits are constructed by Assembler / Decoder:
        // - Opcode 5 (start_bit 0, width 8) -> shift = 16 - 0 - 8 = 8. Bits 15..8.
        // - Dest R0 (start_bit 8, width 3) -> shift = 16 - 8 - 3 = 5. Bits 7..5.
        // - Src R1 (start_bit 11, width 3) -> shift = 16 - 11 - 3 = 2. Bits 4..2.
        // - Padding at end -> 2 bits.
        // Value: (5 << 8) | (0 << 5) | (1 << 2) = 1280 | 0 | 4 = 1284.
        
        uint64_t inst_bits_add = 1284;
        auto decoded_add = decoder.decode(inst_bits_add, 16);
        assert(decoded_add.is_valid);
        assert(decoded_add.name == "ADD");
        assert(decoded_add.opcode == 5);
        assert(decoded_add.regs.count("dest") && decoded_add.regs.at("dest") == 0);
        assert(decoded_add.regs.count("src") && decoded_add.regs.at("src") == 1);

        // Let's decode LDI R2, 100
        // LDI opcode = 2. Layout: [2, "dest", "imm8"]
        // bits: opcode (8), dest (3), imm8 (8) = 19 bits -> 3 bytes = 24 bits fetched.
        // - Opcode 2 (start 0, width 8) -> shift = 24 - 0 - 8 = 16. Bits 23..16.
        // - Dest R2 (start 8, width 3) -> shift = 24 - 8 - 3 = 13. Bits 15..13.
        // - Imm8 100 (start 11, width 8) -> shift = 24 - 11 - 8 = 5. Bits 12..5.
        // - Padding -> 5 bits.
        // Value: (2 << 16) | (2 << 13) | (100 << 5) = 131072 | 16384 | 3200 = 150656.

        uint64_t inst_bits_ldi = 150656;
        auto decoded_ldi = decoder.decode(inst_bits_ldi, 24);
        assert(decoded_ldi.is_valid);
        assert(decoded_ldi.name == "LDI");
        assert(decoded_ldi.opcode == 2);
        assert(decoded_ldi.regs.count("dest") && decoded_ldi.regs.at("dest") == 2);
        assert(decoded_ldi.imms.count("imm8") && decoded_ldi.imms.at("imm8") == 100);

        // 4. Test error handling for invalid / unknown opcode
        // Opcode 99 does not exist in 8bit.json
        // Construct 16-bit word starting with 99 (0x63)
        uint64_t inst_bits_invalid = (99ULL << 8);
        auto decoded_invalid = decoder.decode(inst_bits_invalid, 16);
        assert(!decoded_invalid.is_valid);
        assert(decoded_invalid.error.find("Unknown opcode: 99") != std::string::npos);

        // 5. Test error handling for out of bound register index
        // Reg field width is 3 bits (0 to 7 index).
        // Let's pass register index 7 to ADD (which has only 7 registers total, index 0 to 6).
        // registers: general: R0-R3 (indices 0..3), special: PC, SP, FLAGS (indices 4..6).
        // So index 7 is out of bounds (registers size is 7).
        // Value: (5 << 8) | (7 << 5) | (1 << 2) = 1280 | 224 | 4 = 1508.
        uint64_t inst_bits_bad_reg = 1508;
        auto decoded_bad_reg = decoder.decode(inst_bits_bad_reg, 16);
        assert(!decoded_bad_reg.is_valid);
        assert(decoded_bad_reg.error.find("Invalid register index 7") != std::string::npos);

        std::cout << "Decoder unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
