#include "core/config.hpp"
#include <cassert>
#include <iostream>

int main() {
    try {
        // Test parsing minimal config
        auto cfg = Config::from_file("../configs/tiny4.json");

        assert(cfg.name == "Tiny4");
        assert(cfg.data_width == 4);
        assert(cfg.addr_width == 4);
        assert(cfg.memory_size == 16);
        assert(cfg.registers.size() >= 3);    // A, B, PC
        assert(cfg.alu_ops.size() >= 2);      // ADD, SUB
        assert(cfg.instructions.size() >= 3); // MOV, ADD, HLT

        // Test validation
        assert(cfg.validate() == true);

        // Print parsed config
        std::cout << "Config loaded successfully:\n";
        std::cout << "  Name: " << cfg.name << "\n";
        std::cout << "  Data width: " << cfg.data_width << " bits\n";
        std::cout << "  Address width: " << cfg.addr_width << " bits\n";
        std::cout << "  Memory size: " << cfg.memory_size << " bytes\n";
        std::cout << "  Registers: ";
        for (const auto &r : cfg.registers) {
            std::cout << r.name << "(" << r.width << "bit) ";
        }
        std::cout << "\n";
        std::cout << "  ALU ops: ";
        for (const auto &op : cfg.alu_ops) {
            std::cout << op.name << " ";
        }
        std::cout << "\n";
        std::cout << "  Instructions: ";
        for (const auto &ins : cfg.instructions) {
            std::cout << ins.name << " ";
        }
        std::cout << "\n";

        std::cout << "\nAll tests passed!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
