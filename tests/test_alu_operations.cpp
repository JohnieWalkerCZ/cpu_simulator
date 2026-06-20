#include "core/assembly/assembler.hpp"
#include "core/cpu.hpp"
#include <cassert>
#include <iostream>

int main() {
    try {
        auto cfg = Config::from_file("../configs/8bit.json");
        CPU cpu(cfg);
        auto &regs = cpu.get_registers();
        auto &mem = cpu.get_memory();

        int sp_idx = regs.find_by_role("stack_pointer");
        assert(sp_idx != -1);

        // 1. Test Direct ALU operations
        auto res_16 = cpu.get_alu().execute("SUB", 65535, 2, 0, 16);
        assert(res_16.value == 65533);

        auto res_8 = cpu.get_alu().execute("SUB", 5, 2, 0, 8);
        assert(res_8.value == 3);

        // 2. Wrap-around behaviors
        auto res_8_wrap = cpu.get_alu().execute("SUB", 0, 1, 0, 8);
        assert(res_8_wrap.value == 255);

        // 2b. >64-bit width: ALU must compute and mask correctly using the
        // full word_t range, not just the low 64 bits.
        word_t a_wide = parse_word("0xFFFFFFFFFFFFFFFF0000000000000001");
        word_t b_wide = parse_word("0x00000000000000000000000000000002");
        auto res_100 = cpu.get_alu().execute("ADD", a_wide, b_wide, 0, 100);
        word_t expected_100 = (a_wide + b_wide) & mask_for_width(100);
        assert(res_100.value == expected_100);
        // Sanity check that the high bits (above bit 63) genuinely survived
        // the round trip, i.e. this isn't silently a 64-bit computation.
        assert((res_100.value >> 64) != 0);

        // 3. Assemble and execute PUSH / POP
        Assembler assembler(cfg);
        std::vector<uint8_t> code = assembler.assemble("LDI R0, 42\n"
                                                       "PUSH R0\n"
                                                       "HLT",
                                                       0);

        cpu.load_program(code, 0);
        cpu.reset();

        regs.write(sp_idx, 65535);

        // Execute LDI R0, 42
        cpu.step();
        assert(regs.read("R0") == 42);

        // Execute PUSH R0
        cpu.step();
        assert(regs.read(sp_idx) == 65534); // SP decremented
        assert(mem.read(65534) == 42);      // Pushed value verified

        std::cout << "ALU operations and masking tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
