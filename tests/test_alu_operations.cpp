#include "core/assembly/assembler.hpp"
#include "core/cpu.hpp"
#include <cassert>
#include <iostream>

int main() {
    try {
        // Load the 8-bit config file
        auto cfg = Config::from_file("../configs/8bit.json");
        assert(cfg.name == "8-Bit");

        // Initialize CPU
        CPU cpu(cfg);
        auto &regs = cpu.get_registers();
        auto &mem = cpu.get_memory();

        // 1. Verify register widths
        int sp_idx = regs.find_by_role("stack_pointer");
        assert(sp_idx != -1);
        assert(regs.get_defs()[sp_idx].width == 16);

        int r0_idx = -1;
        for (size_t i = 0; i < regs.get_defs().size(); ++i) {
            if (regs.get_defs()[i].name == "R0") {
                r0_idx = i;
                break;
            }
        }
        assert(r0_idx != -1);
        assert(regs.get_defs()[r0_idx].width == 8);

        // 2. Perform direct ALU operations using the updated execute function
        // 16-bit operation for SP width
        auto res_16 = cpu.get_alu().execute("SUB", 65535, 2, 0, 16);
        assert(res_16.value == 65533);

        // 8-bit operation for GP register width
        auto res_8 = cpu.get_alu().execute("SUB", 5, 2, 0, 8);
        assert(res_8.value == 3);

        // Wrap-around behaviors under 8-bit vs 16-bit masks
        auto res_8_wrap = cpu.get_alu().execute("SUB", 0, 1, 0, 8);
        assert(res_8_wrap.value == 255);

        auto res_16_wrap = cpu.get_alu().execute("SUB", 0, 1, 0, 16);
        assert(res_16_wrap.value == 65535);

        // 3. Assemble and execute instruction sequence to test PUSH / POP masking
        Assembler assembler(cfg);
        std::vector<uint8_t> code = assembler.assemble(
            "LDI R0, 42\n"
            "PUSH R0\n"
            "HLT", 
            0
        );

        cpu.load_program(code, 0);
        cpu.reset();

        // Write initial SP
        regs.write(sp_idx, 65535);

        // Run LDI R0, 42
        cpu.step();
        assert(regs.read("R0") == 42);
        assert(regs.read(sp_idx) == 65535);

        // Run PUSH R0
        cpu.step();
        
        // Stack Pointer should be decremented by 1 (SP = 65534)
        assert(regs.read(sp_idx) == 65534);
        assert(mem.read(65534) == 42);

        // Step once more to run HLT
        cpu.step();
        assert(cpu.is_halted());

        // 4. Test CALL instruction (decrements SP by 2)
        std::vector<uint8_t> code_call = assembler.assemble(
            "CALL #0x1000\n"
            "HLT",
            0
        );

        cpu.load_program(code_call, 0);
        cpu.reset();

        regs.write(sp_idx, 65535);
        
        // Execute CALL (runs 3 uops: SUB $SP #2, mem_write $SP $NEXT_PC, branch)
        cpu.step();

        // SP should be decremented by 2 (SP = 65533)
        assert(regs.read(sp_idx) == 65533);
        
        // Since PC incremented (next instruction address is 3 because CALL occupies 3 bytes: 1 opcode + 2 address)
        // Wait, CALL encodes [11, "address"] -> opcode (8 bits), address (16 bits) -> 24 bits -> 3 bytes
        // So NEXT_PC should be 3
        assert(mem.read(65533) == 3);

        std::cout << "ALU operations and masking tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
