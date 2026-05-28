#include "core/assembly/assembler.hpp"
#include "core/config.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        // Load config
        auto cfg = Config::from_file("../configs/8bit.json");

        // Initialize Assembler
        Assembler assembler(cfg);

        // 1. Test basic assembling of instructions
        std::string source = 
            "start:\n"
            "  LDI R0, 42  ; Load 42 to R0\n"
            "  LDI R1, 10\n"
            "  ADD R0, R1\n"
            "  HLT\n";

        auto code = assembler.assemble(source, 0);

        // Verify size
        // LDI (3 bytes) * 2 + ADD (2 bytes) + HLT (1 byte) = 9 bytes
        assert(code.size() == 9);

        // 2. Verify assembled instruction values
        // Byte 0: LDI opcode = 2
        // LDI R0, 42 has value (2 << 16) | (0 << 13) | (42 << 5) = 131072 | 0 | 1344 = 132416 (0x020540)
        // Bytes are: 0x02, 0x05, 0x40
        assert(code[0] == 0x02);
        assert(code[1] == 0x05);
        assert(code[2] == 0x40);

        // LDI R1, 10 has value (2 << 16) | (1 << 13) | (10 << 5) = 131072 | 8192 | 320 = 139584 (0x022140)
        // Bytes are: 0x02, 0x21, 0x40
        assert(code[3] == 0x02);
        assert(code[4] == 0x21);
        assert(code[5] == 0x40);

        // ADD R0, R1 has value (5 << 8) | (0 << 5) | (1 << 2) = 1284 (0x0504)
        // Bytes are: 0x05, 0x04
        assert(code[6] == 0x05);
        assert(code[7] == 0x04);

        // HLT opcode = 15
        assert(code[8] == 15);

        // 3. Test Labels resolution
        std::string label_source =
            "  JMP target\n"
            "  NOP\n"
            "target:\n"
            "  HLT\n";

        // JMP encoding is [7, "address"]. Opcode = 7. Address width = 16 bits.
        // Opcode 7 (8 bits) + Address (16 bits) = 24 bits = 3 bytes.
        // NOP is 1 byte.
        // Total bytes before target is 3 (JMP) + 1 (NOP) = 4 bytes.
        // So target address must resolve to 4.
        auto code_labels = assembler.assemble(label_source, 0);
        assert(code_labels.size() == 5); // 3 (JMP) + 1 (NOP) + 1 (HLT)
        
        // JMP target value: (7 << 16) | (4 << 0) = 458752 | 4 = 458756 (0x070004)
        // Bytes: 0x07, 0x00, 0x04
        assert(code_labels[0] == 0x07);
        assert(code_labels[1] == 0x00);
        assert(code_labels[2] == 0x04);
        assert(code_labels[3] == 0); // NOP opcode is 0
        assert(code_labels[4] == 15); // HLT opcode is 15

        // 4. Test error handling - Unknown instruction
        try {
            assembler.assemble("XYZ R0, R1", 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            assert(std::string(e.what()).find("Unknown instruction") != std::string::npos);
        }

        try {
            assembler.assemble("LDI R0", 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            assert(std::string(e.what()).find("Unknown instruction") != std::string::npos);
        }

        std::cout << "Assembler unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
