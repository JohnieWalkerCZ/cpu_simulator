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
        std::string source = "start:\n"
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
        // LDI R0, 42 has value (2 << 16) | (0 << 12) | (42 << 4) = 131072 | 0 |
        // 672 = 131744 (0x0202A0) Bytes are: 0x02, 0x02, 0xA0
        assert(code[0] == 0x02);
        assert(code[1] == 0x02);
        assert(code[2] == 0xA0);

        // LDI R1, 10 has value (2 << 16) | (9 << 12) | (10 << 4) = 131072 |
        // 36864 | 160 = 168096 (0x0290A0) Bytes are: 0x02, 0x90, 0xA0
        assert(code[3] == 0x02);
        assert(code[4] == 0x90);
        assert(code[5] == 0xA0);

        // ADD R0, R1 has value (5 << 8) | (0 << 4) | (9 << 0) = 1289 (0x0509)
        // Bytes are: 0x05, 0x09
        assert(code[6] == 0x05);
        assert(code[7] == 0x09);

        // HLT opcode = 15
        assert(code[8] == 15);

        // 3. Test Labels resolution
        std::string label_source = "  JMP target\n"
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

        // JMP target value: (7 << 16) | (4 << 0) = 458752 | 4 = 458756
        // (0x070004) Bytes: 0x07, 0x00, 0x04
        assert(code_labels[0] == 0x07);
        assert(code_labels[1] == 0x00);
        assert(code_labels[2] == 0x04);
        assert(code_labels[3] == 0);  // NOP opcode is 0
        assert(code_labels[4] == 15); // HLT opcode is 15

        // 4. Test error handling - Unknown instruction
        try {
            assembler.assemble("XYZ R0, R1", 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            assert(std::string(e.what()).find("Unknown instruction") !=
                   std::string::npos);
        }

        try {
            assembler.assemble("LDI R0", 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            assert(std::string(e.what()).find("Unknown instruction") !=
                   std::string::npos);
        }

        // 5. Test .equ constant definitions resolve identically to the
        // equivalent literal immediate
        {
            std::string equ_source = ".equ MY_CONST, 0xF0\n"
                                     "LDI R0, MY_CONST\n"
                                     "HLT\n";
            std::string literal_source = "LDI R0, 0xF0\n"
                                         "HLT\n";
            auto equ_code = assembler.assemble(equ_source, 0);
            auto literal_code = assembler.assemble(literal_source, 0);
            assert(equ_code == literal_code);
        }

        // 6. Test .db emits raw bytes and advances the address counter
        {
            std::string db_source = "data:\n"
                                    "  .db 0x41, 66, 0x43\n"
                                    "  JMP data\n";
            auto code = assembler.assemble(db_source, 0);
            assert(code.size() == 6); // 3 data bytes + 3-byte JMP
            assert(code[0] == 0x41);
            assert(code[1] == 66);
            assert(code[2] == 0x43);
            assert(code[3] == 0x07); // JMP opcode
            assert(code[4] == 0x00); // address high byte (target = 0)
            assert(code[5] == 0x00); // address low byte
        }

        // 7. Test .dw emits 2 bytes per value, respecting config endianness
        // (8bit.json is little-endian)
        {
            std::string dw_source = ".dw 0x1234\n";
            auto code = assembler.assemble(dw_source, 0);
            assert(code.size() == 2);
            assert(code[0] == 0x34);
            assert(code[1] == 0x12);
        }

        // 8. Test .ascii emits raw character bytes with no null terminator
        {
            std::string ascii_source = "msg:\n"
                                       "  .ascii \"Hi\"\n"
                                       "  JMP msg\n";
            auto code = assembler.assemble(ascii_source, 0);
            assert(code.size() == 5); // 2 ascii bytes + 3-byte JMP
            assert(code[0] == 'H');
            assert(code[1] == 'i');
            assert(code[2] == 0x07);
        }

        // 8b. Test .byte/.word/.string as aliases producing identical output
        // to .db/.dw/.ascii, with .string additionally null-terminating
        {
            std::string byte_source = ".byte 0x41, 66, 0x43\n";
            std::string db_equiv = ".db 0x41, 66, 0x43\n";
            assert(assembler.assemble(byte_source, 0) ==
                   assembler.assemble(db_equiv, 0));

            std::string word_source = ".word 0x1234\n";
            std::string dw_equiv = ".dw 0x1234\n";
            assert(assembler.assemble(word_source, 0) ==
                   assembler.assemble(dw_equiv, 0));

            std::string string_source = "msg:\n"
                                        "  .string \"Hi\"\n"
                                        "  JMP msg\n";
            auto code = assembler.assemble(string_source, 0);
            assert(code.size() == 6); // "Hi" + null terminator + 3-byte JMP
            assert(code[0] == 'H');
            assert(code[1] == 'i');
            assert(code[2] == 0x00);
            assert(code[3] == 0x07); // JMP opcode
        }

        // 8c. Wide immediate (>64 bits): assemble an instruction with an
        // 88-bit address operand and verify it round-trips through the
        // decoder correctly -- this used to be impossible through a
        // uint64_t encoding pipeline.
        {
            nlohmann::json wide_j = {
                {"name", "WideISA"},
                {"data_bus", {{"width", 8}}},
                {"address_bus", {{"width", 88}}},
                {"memory", {{"size", 1024}}},
                {"registers", {
                    {"special", {
                        {{"name", "PC"}, {"width", 16}, {"initial", 0}, {"role", "program_counter"}}
                    }}
                }},
                {"instruction_set", {
                    {"instructions", {
                        {
                            {"name", "WIDE"}, {"opcode", 7},
                            {"encoding", {7, "address"}},
                            {"microcode", {
                                {{"action", "halt"}}
                            }}
                        }
                    }}
                }}
            };
            Config wide_cfg = Config::from_json(wide_j);
            Assembler wide_assembler(wide_cfg);
            Decoder wide_decoder(wide_cfg);

            word_t address_val = parse_word("0xABCDEF0123456789AB");
            auto wide_code = wide_assembler.assemble(
                "WIDE " + word_to_dec_string(address_val), 0);

            assert(wide_code.size() == 12); // (8 + 88) / 8 bits = 12 bytes

            word_t raw = 0;
            for (uint8_t b : wide_code)
                raw = (raw << 8) | b;

            auto decoded = wide_decoder.decode(raw, 96);
            assert(decoded.is_valid);
            assert(decoded.imms.at("address") == address_val);
        }

        // 9. Test directive error handling
        try {
            assembler.assemble(".equ X, 1\n.equ X, 2\n", 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            assert(std::string(e.what()).find("Duplicate symbol") !=
                   std::string::npos);
        }

        try {
            assembler.assemble("X:\n.equ X, 5\nHLT\n", 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            assert(std::string(e.what()).find("Duplicate symbol") !=
                   std::string::npos);
        }

        try {
            assembler.assemble(".foo 1, 2\n", 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            assert(std::string(e.what()).find("Unknown directive") !=
                   std::string::npos);
        }

        std::cout << "Assembler unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
