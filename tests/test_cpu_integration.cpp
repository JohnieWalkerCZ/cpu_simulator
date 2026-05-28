#include "core/assembly/assembler.hpp"
#include "core/cpu.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    try {
        // Load config
        auto cfg = Config::from_file("../configs/8bit.json");

        // ==========================================
        // Test 1: Fibonacci Loop Program
        // ==========================================
        {
            CPU cpu(cfg);
            Assembler assembler(cfg);

            // Compute first few Fibonacci numbers: 1, 1, 2, 3, 5, 8
            // R0 = current, R1 = next, R2 = temp, R3 = counter
            std::string fib_src =
                "LDI R0, 1\n"
                "LDI R1, 1\n"
                "LDI R3, 4\n" // 4 iterations
                "loop:\n"
                "JZ end\n"
                "MOV R2, R1\n"
                "ADD R1, R0\n"
                "MOV R0, R2\n"
                "LDI R2, 1\n"
                "SUB R3, R2\n" // updates Z flag
                "JMP loop\n"
                "end:\n"
                "HLT\n";

            auto fib_code = assembler.assemble(fib_src, 0);
            cpu.load_program(fib_code, 0);
            cpu.reset();

            // Run program
            cpu.run(1000);

            // Verify final register state
            assert(cpu.is_halted());
            assert(cpu.get_registers().read("R0") == 5);
            assert(cpu.get_registers().read("R1") == 8);
            assert(cpu.get_registers().read("R3") == 0);
            std::cout << "Integration Test 1 (Fibonacci) passed.\n";
        }

        // ==========================================
        // Test 2: MMIO Peripherals Integration
        // ==========================================
        {
            CPU cpu(cfg);
            Assembler assembler(cfg);

            // Register MMIO read/write callbacks for address 0x00AA
            // (within SerialTerminal range 0x00AA-0x00AF in 8bit.json)
            std::string console_output = "";
            cpu.get_memory().map_io_region(
                0x00AA, 0x00AA,
                nullptr,
                [&console_output](uint32_t addr, uint64_t val) {
                    console_output += static_cast<char>(val);
                }
            );

            // Assembly program writing 'H' (72), 'E' (69), 'Y' (89) to 0x00AA
            // Using SP as temporary 16-bit address container
            std::string mmio_src =
                "LDI SP, 0x00AA\n"
                "LDI R0, 72\n"   // 'H'
                "STORE R0, SP\n"
                "LDI R0, 69\n"   // 'E'
                "STORE R0, SP\n"
                "LDI R0, 89\n"   // 'Y'
                "STORE R0, SP\n"
                "HLT\n";

            auto mmio_code = assembler.assemble(mmio_src, 0);
            cpu.load_program(mmio_code, 0);
            cpu.reset();

            // Run
            cpu.run(1000);

            // Verify
            assert(cpu.is_halted());
            assert(console_output == "HEY");
            std::cout << "Integration Test 2 (MMIO Peripherals) passed.\n";
        }

        // ==========================================
        // Test 3: Stack and Subroutines (CALL/RET)
        // ==========================================
        {
            CPU cpu(cfg);
            Assembler assembler(cfg);

            std::string call_src =
                "CALL sub_routine\n"
                "HLT\n"
                "sub_routine:\n"
                "LDI R0, 99\n"
                "RET\n";

            auto call_code = assembler.assemble(call_src, 0);
            cpu.load_program(call_code, 0);
            cpu.reset();

            // Run
            cpu.run(1000);

            // Verify
            assert(cpu.is_halted());
            assert(cpu.get_registers().read("R0") == 99);
            assert(cpu.get_registers().read("SP") == 65535); // Should return to default initial SP (65535)
            std::cout << "Integration Test 3 (CALL/RET) passed.\n";
        }

        std::cout << "All CPU integration tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Integration test failed: " << e.what() << "\n";
        return 1;
    }
}
