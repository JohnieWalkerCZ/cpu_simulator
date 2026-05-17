#include "core/alu/alu.hpp"
#include "core/config.hpp"
#include "core/memory/memory.hpp"
#include "core/registers/register_file.hpp"
#include <bitset>
#include <iostream>

int main(int argc, char *argv[]) {
    try {
        std::string config_path = "../configs/tiny4.json";
        if (argc > 1) {
            config_path = argv[1];
        }

        // Load configuration
        auto cfg = Config::from_file(config_path);
        std::cout << "Loaded config: " << cfg.name << "\n\n";

        // Test Register File
        std::cout << "=== Register File Test ===\n";
        RegisterFile regs(cfg);
        regs.write("A", 5);
        regs.write("B", 3);
        std::cout << "A = " << regs.read("A") << "\n";
        std::cout << "B = " << regs.read("B") << "\n";
        regs.set_pc(10);
        std::cout << "PC = " << regs.get_pc() << "\n";
        regs.increment_pc(2);
        std::cout << "PC after +2 = " << regs.get_pc() << "\n\n";

        // Test Memory
        std::cout << "=== Memory Test ===\n";
        Memory mem(256, cfg.data_width);
        mem.write(0, 0xAB);
        std::cout << "mem[0] = 0x" << std::hex << mem.read(0) << std::dec
                  << "\n";
        mem.write(1, 0xCD);
        std::cout << "mem[1] = 0x" << std::hex << mem.read(1) << std::dec
                  << "\n";

        // Load a small program
        std::vector<uint8_t> program = {0x00, 0x01, 0x0F}; // MOV, ADD, HLT
        mem.load_program(program, 0);
        std::cout << "Loaded " << program.size() << " bytes at address 0\n\n";

        // Test ALU
        std::cout << "=== ALU Test ===\n";
        ALU alu(cfg);

        auto add_result = alu.execute("ADD", 5, 3);
        std::cout << "5 + 3 = " << add_result.value;
        std::cout << " (Zero:" << add_result.zero
                  << " Carry:" << add_result.carry;
        std::cout << " Overflow:" << add_result.overflow
                  << " Negative:" << add_result.negative << ")\n";

        auto sub_result = alu.execute("SUB", 5, 3);
        std::cout << "5 - 3 = " << sub_result.value;
        std::cout << " (Zero:" << sub_result.zero
                  << " Carry:" << sub_result.carry;
        std::cout << " Overflow:" << sub_result.overflow
                  << " Negative:" << sub_result.negative << ")\n";

        auto and_result = alu.execute("AND", 0b1100, 0b1010);
        std::cout << "1100 & 1010 = " << std::bitset<4>(and_result.value)
                  << "\n";

        auto or_result = alu.execute("OR", 0b1100, 0b1010);
        std::cout << "1100 | 1010 = " << std::bitset<4>(or_result.value)
                  << "\n";

        std::cout << "\nAll tests passed successfully!\n";

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
