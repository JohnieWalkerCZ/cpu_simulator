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

        // 4. ALU mini-language programs: ops defined with loops/conditionals
        {
            ALUOp mul;
            mul.name = "MUL";
            mul.code = 20;
            mul.latency = 1;
            mul.program = {"acc = 0;", "for i in 0..width {",
                           "  if ((b >> i) & 1) { acc = acc + (a << i); }",
                           "}", "return acc;"};
            mul.flag_rules = {{"Z", "result_zero"}};
            cfg.alu_ops.push_back(mul);

            ALUOp dot4;
            dot4.name = "DOT4";
            dot4.code = 21;
            dot4.latency = 1;
            dot4.program = {
                "acc = 0;", "for i in 0..4 {",
                "  acc = acc + ((a >> (i * 8)) & 0xFF) * ((b >> (i * 8)) & 0xFF);",
                "}", "return acc;"};
            cfg.alu_ops.push_back(dot4);

            ALUOp popcnt;
            popcnt.name = "POPCNT";
            popcnt.code = 22;
            popcnt.latency = 1;
            popcnt.program = {"n = a;", "c = 0;", "while (n) {",
                              "  c = c + (n & 1);", "  n = n >> 1;", "}",
                              "return c;"};
            cfg.alu_ops.push_back(popcnt);

            ALUOp maxop;
            maxop.name = "MAX";
            maxop.code = 23;
            maxop.latency = 1;
            maxop.program = {"if (a >= b) { return a; } else { return b; }"};
            cfg.alu_ops.push_back(maxop);

            assert(cfg.validate());

            CPU cpu2(cfg);
            auto &alu2 = cpu2.get_alu();

            // shift-and-add multiplication
            auto r1 = alu2.execute("MUL", 13, 11, 0, 8);
            assert(r1.value == 143);
            auto r2 = alu2.execute("MUL", 0, 123, 0, 8);
            assert(r2.value == 0);
            assert((r2.flags_register & 1) != 0); // Z flag set
            auto r3 = alu2.execute("MUL", 200, 2, 0, 8);
            assert(r3.value == 144); // 400 mod 256

            // dot product of 4 packed bytes
            auto r4 = alu2.execute("DOT4", 0x01020304, 0x01010101, 0, 32);
            assert(r4.value == 10);

            // popcount via while loop
            auto r5 = alu2.execute("POPCNT", 0b101101, 0, 0, 8);
            assert(r5.value == 4);

            // max via if/else and comparison operators
            assert(alu2.execute("MAX", 5, 9, 0, 8).value == 9);
            assert(alu2.execute("MAX", 9, 5, 0, 8).value == 9);

            // new comparison/logical operators also work in plain expressions
            assert(ALU::is_valid_expression("a == b"));
            assert(ALU::is_valid_expression("(a > b) && (c != 0)"));

            // invalid programs are rejected
            assert(!ALU::is_valid_program({"for i in {", "}"}));
            assert(!ALU::is_valid_program({"x = ;"}));
            assert(ALU::is_valid_program({"if (a) { return 1; }"}));
        }

        // 5. Infinite loops in programs abort via the step limit
        {
            Config cfg3 = cfg;
            ALUOp hang;
            hang.name = "HANG";
            hang.code = 30;
            hang.latency = 1;
            hang.program = {"while (1) { x = 1; }", "return 0;"};
            cfg3.alu_ops.push_back(hang);
            ALU alu3(cfg3);
            bool threw = false;
            try {
                alu3.execute("HANG", 0, 0, 0, 8);
            } catch (const std::runtime_error &) {
                threw = true;
            }
            assert(threw);
        }

        std::cout << "ALU operations and masking tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
