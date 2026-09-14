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

        // ==========================================
        // Test 4: End-to-end execution with a >64-bit register
        // ==========================================
        {
            nlohmann::json wide_j = {
                {"name", "WideISA"},
                {"data_bus", {{"width", 8}}},
                {"address_bus", {{"width", 16}}},
                {"memory", {{"size", 1024}}},
                {"registers", {
                    {"general_purpose", {
                        {{"name", "R0"}, {"width", 72}, {"initial", 0}},
                        {{"name", "R1"}, {"width", 72}, {"initial", 0}}
                    }},
                    {"special", {
                        {{"name", "PC"}, {"width", 16}, {"initial", 0}, {"role", "program_counter"}}
                    }}
                }},
                {"alu", {
                    {"operations", {
                        {{"name", "ADD"}, {"code", 1}, {"expression", "a + b"}, {"latency", 1}}
                    }}
                }},
                {"instruction_set", {
                    {"instructions", {
                        {
                            {"name", "ADD"}, {"opcode", 1},
                            {"encoding", {1, "dest", "src"}},
                            {"microcode", {
                                {{"action", "alu"}, {"op", "ADD"},
                                 {"a", "@dest"}, {"b", "@src"}, {"out", "@dest"}}
                            }}
                        }
                    }}
                }}
            };
            Config wide_cfg = Config::from_json(wide_j);
            assert(wide_cfg.validate());

            CPU wide_cpu(wide_cfg);
            Assembler wide_assembler(wide_cfg);

            auto wide_code = wide_assembler.assemble("ADD R0, R1", 0);
            wide_cpu.load_program(wide_code, 0);
            wide_cpu.reset();

            word_t r0_init = parse_word("0xFFFFFFFFFFFFFFFF00"); // > 64 bits
            word_t r1_init = parse_word("0x0000000000000000FF");
            wide_cpu.get_registers().write("R0", r0_init);
            wide_cpu.get_registers().write("R1", r1_init);

            wide_cpu.step(); // Execute ADD R0, R1

            word_t expected = (r0_init + r1_init) & mask_for_width(72);
            assert(wide_cpu.get_registers().read("R0") == expected);
            assert((expected >> 64) != 0); // confirms high bits are real

            std::cout
                << "Integration Test 4 (>64-bit register ADD) passed.\n";
        }

        // ==========================================
        // Test 5: Writing $SP/$FLAGS on a config with no stack_pointer or
        // status_flags role register must not crash (regression: write_operand
        // used to skip the -1 guard that resolve_operand already had).
        // ==========================================
        {
            nlohmann::json no_sp_j = {
                {"name", "NoSpIsa"},
                {"data_bus", {{"width", 8}}},
                {"address_bus", {{"width", 16}}},
                {"memory", {{"size", 256}}},
                {"registers", {
                    {"general_purpose", {
                        {{"name", "R0"}, {"width", 8}, {"initial", 0}}
                    }},
                    {"special", {
                        {{"name", "PC"}, {"width", 16}, {"initial", 0}, {"role", "program_counter"}}
                    }}
                }},
                {"alu", {{"operations", nlohmann::json::array()}}},
                {"instruction_set", {
                    {"instructions", {
                        {
                            {"name", "SETSP"}, {"opcode", 1},
                            {"encoding", {1}},
                            {"microcode", {
                                {{"action", "copy"}, {"source", "#5"}, {"dest", "$SP"}},
                                {{"action", "copy"}, {"source", "#1"}, {"dest", "$FLAGS"}}
                            }}
                        }
                    }}
                }}
            };
            Config no_sp_cfg = Config::from_json(no_sp_j);
            assert(no_sp_cfg.validate());

            CPU cpu(no_sp_cfg);
            Assembler assembler(no_sp_cfg);

            auto code = assembler.assemble("SETSP", 0);
            cpu.load_program(code, 0);
            cpu.reset();

            cpu.step(); // Must not throw despite no SP/FLAGS register existing.

            std::cout << "Integration Test 5 (write to $SP/$FLAGS with no "
                        "role register) passed.\n";
        }

        // ==========================================
        // Test 6: Latency modes (STRICT/BOTTLENECK/ADDITIVE) actually change
        // how many clock cycles an instruction's EXECUTE_UOPS phase takes,
        // relative to plain DYNAMIC sequencing. None of these modes had any
        // test coverage before, despite being a documented, user-facing
        // config feature (CONFIGURATION.md 5.2).
        // ==========================================
        {
            auto build_cfg = [](const nlohmann::json &instructions,
                                const nlohmann::json &alu_ops) {
                nlohmann::json j = {
                    {"name", "LatencyISA"},
                    {"data_bus", {{"width", 8}}},
                    {"address_bus", {{"width", 8}}},
                    {"memory", {{"size", 256}}},
                    {"registers", {
                        {"general_purpose", {
                            {{"name", "R0"}, {"width", 8}, {"initial", 0}}
                        }},
                        {"special", {
                            {{"name", "PC"}, {"width", 8}, {"initial", 0}, {"role", "program_counter"}}
                        }}
                    }},
                    {"alu", {{"operations", alu_ops}}},
                    {"instruction_set", {{"instructions", instructions}}}
                };
                return Config::from_json(j);
            };

            auto three_copies = nlohmann::json::array(
                {{{"action", "copy"}, {"source", "#1"}, {"dest", "$R0"}},
                {{"action", "copy"}, {"source", "#2"}, {"dest", "$R0"}},
                {{"action", "copy"}, {"source", "#3"}, {"dest", "$R0"}}});

            // --- DYNAMIC baseline: 3 uops take 3 separate EXECUTE_UOPS
            // cycles (one uop per clock).
            int dynamic_cycles;
            {
                Config cfg_dyn = build_cfg(
                    {{{"name", "DYN"}, {"opcode", 1}, {"encoding", {1}},
                      {"microcode", three_copies}}},
                    nlohmann::json::array());
                CPU cpu(cfg_dyn);
                Assembler asmb(cfg_dyn);
                cpu.load_program(asmb.assemble("DYN", 0), 0);
                cpu.reset();
                int before = cpu.get_executor().get_total_cycles();
                cpu.step();
                dynamic_cycles = cpu.get_executor().get_total_cycles() - before;
                assert(cpu.get_registers().read("R0") == 3);
            }

            // --- STRICT with latency=1: must compress all 3 uops into a
            // single EXECUTE_UOPS cycle, so the whole instruction takes
            // fewer total cycles than the DYNAMIC baseline.
            {
                Config cfg_strict = build_cfg(
                    {{{"name", "STR"}, {"opcode", 1}, {"encoding", {1}},
                      {"latency", 1}, {"latency_mode", "strict"},
                      {"microcode", three_copies}}},
                    nlohmann::json::array());
                CPU cpu(cfg_strict);
                Assembler asmb(cfg_strict);
                cpu.load_program(asmb.assemble("STR", 0), 0);
                cpu.reset();
                int before = cpu.get_executor().get_total_cycles();
                cpu.step();
                int strict_cycles = cpu.get_executor().get_total_cycles() - before;

                assert(cpu.get_registers().read("R0") == 3); // all 3 ran
                assert(strict_cycles < dynamic_cycles);
            }

            // --- ADDITIVE with latency=5 on a single-uop instruction: must
            // pad 5 extra structural cycles onto the end, so it takes more
            // total cycles than an equivalent single-uop DYNAMIC instruction.
            {
                auto one_copy = nlohmann::json::array(
                    {{{"action", "copy"}, {"source", "#7"}, {"dest", "$R0"}}});

                Config cfg_base = build_cfg(
                    {{{"name", "ONE"}, {"opcode", 1}, {"encoding", {1}},
                      {"microcode", one_copy}}},
                    nlohmann::json::array());
                CPU base_cpu(cfg_base);
                Assembler base_asmb(cfg_base);
                base_cpu.load_program(base_asmb.assemble("ONE", 0), 0);
                base_cpu.reset();
                int before_base = base_cpu.get_executor().get_total_cycles();
                base_cpu.step();
                int base_cycles =
                    base_cpu.get_executor().get_total_cycles() - before_base;

                Config cfg_add = build_cfg(
                    {{{"name", "ADD5"}, {"opcode", 1}, {"encoding", {1}},
                      {"latency", 5}, {"latency_mode", "additive"},
                      {"microcode", one_copy}}},
                    nlohmann::json::array());
                CPU add_cpu(cfg_add);
                Assembler add_asmb(cfg_add);
                add_cpu.load_program(add_asmb.assemble("ADD5", 0), 0);
                add_cpu.reset();
                int before_add = add_cpu.get_executor().get_total_cycles();
                add_cpu.step();
                int additive_cycles =
                    add_cpu.get_executor().get_total_cycles() - before_add;

                assert(add_cpu.get_registers().read("R0") == 7);
                assert(additive_cycles == base_cycles + 5);
            }

            // --- BOTTLENECK: the execution phase must take the max of the
            // instruction-level latency and the slow ALU functional unit's
            // own latency, not just the instruction latency.
            {
                nlohmann::json slow_alu = nlohmann::json::array(
                    {{{"name", "SLOWADD"}, {"code", 1},
                      {"expression", "a + b"}, {"latency", 4}}});
                auto slow_op = nlohmann::json::array(
                    {{{"action", "alu"}, {"op", "SLOWADD"}, {"a", "#2"},
                      {"b", "#3"}, {"out", "$R0"}}});

                Config cfg_bot = build_cfg(
                    {{{"name", "BOT"}, {"opcode", 1}, {"encoding", {1}},
                      {"latency", 1}, {"latency_mode", "bottleneck"},
                      {"microcode", slow_op}}},
                    slow_alu);
                CPU cpu(cfg_bot);
                Assembler asmb(cfg_bot);
                cpu.load_program(asmb.assemble("BOT", 0), 0);
                cpu.reset();
                int before = cpu.get_executor().get_total_cycles();
                cpu.step();
                int bottleneck_cycles =
                    cpu.get_executor().get_total_cycles() - before;

                assert(cpu.get_registers().read("R0") == 5); // 2 + 3
                // FETCH + DECODE + max(1, 4) EXECUTE_UOPS + DONE = 7, not 4.
                assert(bottleneck_cycles >= 7);
            }

            std::cout << "Integration Test 6 (latency modes: strict/"
                        "bottleneck/additive) passed.\n";
        }

        // ==========================================
        // Test 7: Array sum over .db-defined data using LOAD in a loop
        // (8bit.json). Exercises label-as-immediate resolution, pointer
        // arithmetic, and reading ROM data through the LOAD instruction
        // rather than direct memory pokes.
        // ==========================================
        {
            CPU cpu(cfg);
            Assembler assembler(cfg);

            std::string sum_src = "JMP main\n"
                                   "table:\n"
                                   "  .db 10, 20, 30, 40, 50\n"
                                   "main:\n"
                                   "  LDI R0, table\n" // pointer
                                   "  LDI R3, 5\n"     // counter
                                   "  LDI R2, 0\n"     // sum
                                   "sum_loop:\n"
                                   "  JZ done\n"
                                   "  LOAD R1, R0\n"
                                   "  ADD R2, R1\n"
                                   "  LDI R1, 1\n"
                                   "  ADD R0, R1\n"
                                   "  LDI R1, 1\n"
                                   "  SUB R3, R1\n" // updates Z flag
                                   "  JMP sum_loop\n"
                                   "done:\n"
                                   "  HLT\n";

            auto code = assembler.assemble(sum_src, 0);
            cpu.load_program(code, 0);
            cpu.reset();
            cpu.run(1000);

            assert(cpu.is_halted());
            assert(cpu.get_registers().read("R2") == 150); // 10+20+30+40+50
            assert(cpu.get_registers().read("R3") == 0);
            std::cout
                << "Integration Test 7 (array sum via .db + LOAD loop) passed.\n";
        }

        // ==========================================
        // Test 8: Multiply-accumulate subroutine on the 16-bit RISC config
        // (MUL, SHL, AND, immediate loads, CALL/RET, PUSH/POP) computing
        // R0 = (R0*R1) + ((R2 << 2) & 0xFF).
        // ==========================================
        {
            auto cfg16 = Config::from_file("../configs/16bit.json");
            CPU cpu(cfg16);
            Assembler assembler(cfg16);

            std::string mac_src = "JMP main\n"
                                   "mac:\n"
                                   "  PUSH R4\n"
                                   "  MUL R0, R1\n"
                                   "  SHL R2, 2\n"
                                   "  LDI R4, 0x00FF\n"
                                   "  AND R2, R4\n"
                                   "  ADD R0, R2\n"
                                   "  POP R4\n"
                                   "  RET\n"
                                   "main:\n"
                                   "  LDI R0, 6\n"
                                   "  LDI R1, 7\n"
                                   "  LDI R2, 100\n"
                                   "  CALL mac\n"
                                   "  HLT\n";

            auto code = assembler.assemble(mac_src, 0);
            cpu.load_program(code, 0);
            cpu.reset();

            int sp_idx = cpu.get_registers().find_by_role("stack_pointer");
            word_t sp_before = cpu.get_registers().read(sp_idx);

            cpu.run(1000);

            assert(cpu.is_halted());
            assert(cpu.get_registers().read("R0") == 186); // 6*7 + (400&0xFF)
            assert(cpu.get_registers().read("R4") == 0);   // restored by POP
            assert(cpu.get_registers().read(sp_idx) ==
                   sp_before); // CALL/RET + balanced PUSH/POP: SP unchanged
            std::cout << "Integration Test 8 (MUL/SHL/AND subroutine, "
                        "16bit.json) passed.\n";
        }

        std::cout << "All CPU integration tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Integration test failed: " << e.what() << "\n";
        return 1;
    }
}
