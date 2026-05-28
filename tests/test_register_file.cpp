#include "core/registers/register_file.hpp"
#include "core/config.hpp"
#include <cassert>
#include <iostream>

int main() {
    try {
        // Load the 8-bit config file to test with
        auto cfg = Config::from_file("../configs/8bit.json");

        // Initialize register file
        RegisterFile regs(cfg);

        // 1. Verify sizes and defs
        assert(regs.size() == cfg.registers.size());
        
        // Find indices
        int pc_idx = regs.find_by_role("program_counter");
        int sp_idx = regs.find_by_role("stack_pointer");
        int flags_idx = regs.find_by_role("status_flags");
        
        assert(pc_idx != -1);
        assert(sp_idx != -1);
        assert(flags_idx != -1);

        // 2. Test reading default initial values
        assert(regs.read("PC") == 0);
        assert(regs.read("SP") == 65535);
        assert(regs.read("FLAGS") == 0);
        assert(regs.read("R0") == 0);

        // 3. Test writing and register width masking
        // SP is 16-bit
        regs.write("SP", 0x12345678ULL);
        assert(regs.read("SP") == 0x5678ULL); // Masked to 16 bits

        // R0 is 8-bit
        regs.write("R0", 0x12345678ULL);
        assert(regs.read("R0") == 0x78ULL); // Masked to 8 bits

        // FLAGS is 8-bit
        regs.write("FLAGS", 0xFFFU);
        assert(regs.read("FLAGS") == 0xFFU); // Masked to 8 bits

        // 4. Test PC specific functions
        assert(regs.get_pc() == 0);
        regs.set_pc(0xDEADBEEF);
        assert(regs.get_pc() == 0xBEEF); // Masked to 16 bits (PC width)

        // Increment PC
        regs.set_pc(65530);
        regs.increment_pc(3);
        assert(regs.get_pc() == 65533);

        // Test increment_pc wrap around
        // Note: increment_pc uses modulo max_value where max_value = (1 << width) - 1.
        // Let's verify standard wrap around in increment_pc (current + amount) % max_value
        // If current is 65533 and amount is 4, new_val = (65533 + 4) % 65535 = 65537 % 65535 = 2.
        regs.set_pc(65533);
        regs.increment_pc(4);
        assert(regs.get_pc() == 2);

        // 5. Test index-based read/write
        regs.write(0, 0xAA); // R0 is usually index 0
        assert(regs.read(0) == 0xAA);

        // 6. Test reset
        regs.write("SP", 100);
        regs.write("R0", 20);
        regs.reset();
        assert(regs.read("SP") == 65535); // Restored to initial
        assert(regs.read("R0") == 0);      // Restored to initial

        // 7. Test invalid registers
        try {
            regs.read("INVALID_REG");
            assert(false); // Should throw
        } catch (const std::runtime_error &e) {
            // Expected
        }

        try {
            regs.write("INVALID_REG", 10);
            assert(false); // Should throw
        } catch (const std::runtime_error &e) {
            // Expected
        }

        std::cout << "RegisterFile unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
