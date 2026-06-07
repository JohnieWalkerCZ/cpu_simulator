#include "core/assembly/assembler.hpp"
#include "core/cpu.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    try {
        auto cfg = Config::from_file("../configs/8bit.json");

        // ==========================================
        // Test 1: Fibonacci Loop
        // ==========================================
        {
            CPU cpu(cfg);
            Assembler assembler(cfg);

            std::string fib_src = "LDI R0, 1\n"
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

            cpu.run(1000);

            assert(cpu.is_halted());
            assert(cpu.get_registers().read("R0") == 5);
            assert(cpu.get_registers().read("R1") == 8);
            assert(cpu.get_registers().read("R3") == 0);
            std::cout << "Integration Test 1 (Fibonacci) passed.\n";
        }

        // ==========================================
        // Test 2: Declarative Peripherals Integration (Co-Processor & UART)
        // ==========================================
        {
            CPU cpu(cfg);

            // Set up a host print hook to record stdout
            std::string serial_out = "";
            for (auto &dp : cpu.get_peripherals()) {
                if (dp.get_name() == "DeclarativeUART") {
                    dp.set_host_print_hook(
                        [&serial_out](char c) { serial_out += c; });
                }
            }

            // Manually trigger the Math Co-Processor MMIO
            // 0x80 = OP_A, 0x81 = OP_B, 0x83 = CMD, 0x82 = RESULT
            cpu.get_memory().write(0x80, 13); // A = 13
            cpu.get_memory().write(0x81, 5);  // B = 5
            cpu.get_memory().write(
                0x83,
                1); // Write 1 to CMD -> triggers AST Multiply: 13 * 5 = 65

            // Read the result
            uint64_t result = cpu.get_memory().read(0x82);
            assert(result == 65); // 65 is ASCII 'A'

            // Write 'A' to the UART peripheral (0xAA)
            cpu.get_memory().write(0xAA, result);

            // Verify our C++ host print hook was triggered by the UART's AST
            // call
            assert(serial_out == "A");
            std::cout
                << "Integration Test 2 (Declarative Peripherals) passed.\n";
        }

        // ==========================================
        // Test 3: Vectored Hardware Interrupts & Stack Context Saving
        // ==========================================
        {
            CPU cpu(cfg);
            Assembler assembler(cfg);

            // Assemble a program with an ISR at address 0x03 and main at 0x07
            std::string interrupt_prog =
                "JMP main\n"     // 0x00: JMP main (3 bytes)
                "isr:\n"         // 0x03: ISR address
                "  LDI R2, 99\n" // 0x03: LDI R2, 99 (3 bytes)
                "  RET\n"        // 0x06: RET (1 byte)
                "main:\n"        // 0x07: Main program
                "  LDI R0, 1\n"  // 0x07: LDI R0, 1 (3 bytes) (SP remains at
                                 // default 65535)
                "loop:\n"        // 0x0A: Endless loop waiting for interrupt
                "  LDI R1, 1\n"
                "  ADD R0, R1\n"
                "  JMP loop\n";

            auto code = assembler.assemble(interrupt_prog, 0);
            cpu.load_program(code, 0);
            cpu.reset();

            int sp_idx = cpu.get_registers().find_by_role("stack_pointer");

            // Run first 2 instructions to enter the loop
            cpu.step(); // Execute JMP main

            cpu.step(); // Execute LDI R0, 1

            assert(cpu.get_registers().read(sp_idx) ==
                   65535); // SP starts at default 65535 (STACK segment)
            assert(cpu.get_registers().get_pc() == 0x0A); // Now inside the loop

            // Trigger hardware interrupt on line 0x03 (points directly to
            // `isr`)
            cpu.trigger_interrupt(0x03);

            // Step the CPU. It should complete the current loop instruction,
            // push the current PC onto the stack, and jump to 0x03.
            cpu.step();

            assert(cpu.get_registers().get_pc() ==
                   0x03); // PC jumped to ISR address
            assert(cpu.get_registers().read(sp_idx) ==
                   65533); // SP decremented (pushed 16-bit PC)

            // Execute ISR: LDI R2, 99
            cpu.step();
            assert(cpu.get_registers().read("R2") == 99);

            // Execute RET: Pops PC from the stack and returns back to the loop!
            cpu.step();
            assert(cpu.get_registers().get_pc() == 0x0A ||
                   cpu.get_registers().get_pc() == 0x0D);      // Resumed loop
            assert(cpu.get_registers().read(sp_idx) == 65535); // Stack restored

            std::cout << "Integration Test 3 (Hardware Interrupts) passed.\n";
        }

        std::cout << "All CPU integration tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Integration test failed: " << e.what() << "\n";
        return 1;
    }
}
