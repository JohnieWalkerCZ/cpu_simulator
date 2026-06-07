#include "core/config.hpp"
#include "core/memory/memory.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

Config create_mock_config(int memory_size, int data_width,
                          std::vector<MemorySegmentDef> segments = {}) {
    Config cfg;
    cfg.name = "TestConfig";
    cfg.memory_size = memory_size;
    cfg.data_width = data_width;
    cfg.addr_width = 16;
    if (segments.empty()) {
        cfg.memory_segments.push_back(
            {"FLAT_RAM", 0, (uint32_t)memory_size - 1, true, true, true});
    } else {
        cfg.memory_segments = segments;
    }
    return cfg;
}

int main() {
    try {
        std::vector<MemorySegmentDef> protected_segments = {
            {"ROM", 0x0000, 0x00FF, true, false, true},  // R=1, W=0, X=1
            {"RAM", 0x0100, 0x01FF, true, true, false},  // R=1, W=1, X=0
            {"STACK", 0x0200, 0x02FF, true, true, false} // R=1, W=1, X=0
        };
        Config cfg = create_mock_config(1024, 8, protected_segments);
        Memory mem(cfg);

        // 1. Test ROM Protection: Write to ROM must throw an access violation
        try {
            mem.write(0x0050, 0xAA);
            assert(false);
        } catch (const std::runtime_error &e) {
            // Expected: Write violation
        }

        // 2. Test ROM Execution: Read with is_execute=true must pass
        try {
            mem.read(0x0050, true);
        } catch (const std::exception &e) {
            assert(false); // Execution should be permitted
        }

        // 3. Test RAM Protection: Execute from RAM must throw an access
        // violation
        try {
            mem.read(0x0150, true); // is_execute = true
            assert(false);
        } catch (const std::runtime_error &e) {
            // Expected: Execute violation
        }

        std::cout << "Memory unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
