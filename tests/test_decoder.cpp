#include "core/assembly/assembler.hpp"
#include "core/config.hpp"
#include "core/execution/decoder.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    try {
        // Load config
        auto cfg = Config::from_file("../configs/8bit.json");

        // Initialize Decoder & Assembler
        Decoder decoder(cfg);
        Assembler assembler(cfg);

        // 1. Verify basic decoder properties
        assert(decoder.get_opcode_width() == 8);
        assert(decoder.peek_opcode(0x05) == 5);

        // Find dynamically assigned register indices
        int r0_idx = -1, r1_idx = -1, r2_idx = -1;
        int r0h_idx = -1, r0l_idx = -1;
        for (size_t i = 0; i < cfg.registers.size(); ++i) {
            if (cfg.registers[i].name == "R0")
                r0_idx = i;
            if (cfg.registers[i].name == "R1")
                r1_idx = i;
            if (cfg.registers[i].name == "R2")
                r2_idx = i;
            if (cfg.registers[i].name == "R0H")
                r0h_idx = i;
            if (cfg.registers[i].name == "R0L")
                r0l_idx = i;
        }
        assert(r0_idx != -1);
        assert(r1_idx != -1);
        assert(r2_idx != -1);
        assert(r0h_idx != -1);
        assert(r0l_idx != -1);

        // 2. Decode ADD R0, R1 (generated dynamically)
        std::vector<uint8_t> code_add = assembler.assemble("ADD R0, R1", 0);
        int total_bits_add = decoder.get_total_bits(5); // ADD opcode is 5
        int units_add = (total_bits_add + cfg.data_width - 1) / cfg.data_width;

        uint64_t inst_bits_add = 0;
        for (int i = 0; i < units_add; ++i) {
            inst_bits_add = (inst_bits_add << cfg.data_width) | code_add[i];
        }

        auto decoded_add =
            decoder.decode(inst_bits_add, units_add * cfg.data_width);
        assert(decoded_add.is_valid);
        assert(decoded_add.name == "ADD");
        assert(decoded_add.opcode == 5);
        assert(decoded_add.regs.count("dest") &&
               decoded_add.regs.at("dest") == r0_idx);
        assert(decoded_add.regs.count("src") &&
               decoded_add.regs.at("src") == r1_idx);

        // 3. Decode LDI R2, 100 (generated dynamically)
        std::vector<uint8_t> code_ldi = assembler.assemble("LDI R2, 100", 0);
        int total_bits_ldi = decoder.get_total_bits(2); // LDI opcode is 2
        int units_ldi = (total_bits_ldi + cfg.data_width - 1) / cfg.data_width;

        uint64_t inst_bits_ldi = 0;
        for (int i = 0; i < units_ldi; ++i) {
            inst_bits_ldi = (inst_bits_ldi << cfg.data_width) | code_ldi[i];
        }

        auto decoded_ldi =
            decoder.decode(inst_bits_ldi, units_ldi * cfg.data_width);
        assert(decoded_ldi.is_valid);
        assert(decoded_ldi.name == "LDI");
        assert(decoded_ldi.opcode == 2);
        assert(decoded_ldi.regs.count("dest") &&
               decoded_ldi.regs.at("dest") == r2_idx);
        assert(decoded_ldi.imms.count("imm8") &&
               decoded_ldi.imms.at("imm8") == 100);

        // 4. Decode with virtual sub-registers: ADD R0H, R0L (generated
        // dynamically)
        std::vector<uint8_t> code_sub = assembler.assemble("ADD R0H, R0L", 0);
        uint64_t inst_bits_sub = 0;
        for (int i = 0; i < units_add; ++i) {
            inst_bits_sub = (inst_bits_sub << cfg.data_width) | code_sub[i];
        }

        auto decoded_sub =
            decoder.decode(inst_bits_sub, units_add * cfg.data_width);
        assert(decoded_sub.is_valid);
        assert(decoded_sub.regs.count("dest") &&
               decoded_sub.regs.at("dest") == r0h_idx);
        assert(decoded_sub.regs.count("src") &&
               decoded_sub.regs.at("src") == r0l_idx);

        // 5. Test error handling for invalid / unknown opcode
        uint64_t inst_bits_invalid =
            (99ULL << (units_add * cfg.data_width - 8));
        auto decoded_invalid =
            decoder.decode(inst_bits_invalid, units_add * cfg.data_width);
        assert(!decoded_invalid.is_valid);
        assert(decoded_invalid.error.find("Unknown opcode: 99") !=
               std::string::npos);

        // 6. Test error handling for out-of-bound register index
        // Dynamically build a bitstream containing an out-of-bound index (total
        // register count)
        int reg_width =
            decoder.calculate_reg_bits(static_cast<int>(cfg.registers.size()));
        int oob_index =
            static_cast<int>(cfg.registers.size()); // Virtual OOB index

        uint64_t raw_oob = 5; // ADD Opcode
        raw_oob = (raw_oob << reg_width) | oob_index;
        raw_oob = (raw_oob << reg_width) | r1_idx;

        int total_bits_oob = 8 + reg_width * 2;
        int units_oob = (total_bits_oob + cfg.data_width - 1) / cfg.data_width;
        raw_oob <<= (units_oob * cfg.data_width - total_bits_oob);

        auto decoded_bad_reg =
            decoder.decode(raw_oob, units_oob * cfg.data_width);
        assert(!decoded_bad_reg.is_valid);
        assert(decoded_bad_reg.error.find("Invalid register index") !=
               std::string::npos);

        std::cout << "Decoder unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
