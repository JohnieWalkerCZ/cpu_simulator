#include "core/config.hpp"
#include "core/memory/memory.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

Config create_mock_config(uint64_t memory_size, int data_width,
                          std::vector<MemorySegmentDef> segments = {},
                          int addr_width = 16) {
    Config cfg;
    cfg.name = "TestConfig";
    cfg.memory_size = memory_size;
    cfg.data_width = data_width;
    cfg.addr_width = addr_width;
    if (segments.empty()) {
        cfg.memory_segments.push_back(
            {"FLAT_RAM", 0, memory_size - 1, true, true, true});
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

        // 4. A 32-bit address bus with a full 4GB of configured memory must
        // not eagerly allocate anything at construction (the original bug
        // this paged design fixes), and reads/writes at the extremes of the
        // address space must still work correctly via lazily-allocated
        // pages.
        {
            Config big_cfg =
                create_mock_config(4294967296ULL, 8, {}, 32);
            Memory big_mem(big_cfg);

            assert(big_mem.size() == 4294967296ULL);

            big_mem.write(0, 0xAB);
            big_mem.write(4294967295ULL, 0xCD);

            assert(big_mem.read(0) == 0xAB);
            assert(big_mem.read(4294967295ULL) == 0xCD);
            assert(big_mem.peek(0) == 0xAB);
            assert(big_mem.peek(4294967295ULL) == 0xCD);
            // An untouched address in between must read as zero, not crash.
            assert(big_mem.peek(2147483648ULL) == 0);

            // Snapshotting must only copy touched pages, not the full 4GB
            // address space.
            auto snap = big_mem.capture_snapshot();
            big_mem.write(100, 0xFF);
            big_mem.restore_snapshot(snap);
            assert(big_mem.read(100) == 0);
            assert(big_mem.read(0) == 0xAB);

            big_mem.reset();
            assert(big_mem.peek(0) == 0);
        }

        // 5. A genuine 64-bit address bus (addr_width=64) with a huge
        // configured size must behave the same way: instant construction,
        // correct reads/writes/peeks at the extremes via lazily-allocated
        // pages, and cheap snapshot/restore.
        {
            uint64_t huge_size = 1ULL << 48; // 256 TB address space
            Config cfg64 = create_mock_config(huge_size, 8, {}, 64);
            Memory mem64(cfg64);

            assert(mem64.size() == huge_size);

            mem64.write(0, 0xAB);
            mem64.write(huge_size - 1, 0xCD);

            assert(mem64.read(0) == 0xAB);
            assert(mem64.read(huge_size - 1) == 0xCD);
            assert(mem64.peek(huge_size - 1) == 0xCD);
            assert(mem64.peek(huge_size / 2) == 0); // untouched, not crash

            auto snap64 = mem64.capture_snapshot();
            mem64.write(42, 0xFF);
            mem64.restore_snapshot(snap64);
            assert(mem64.read(42) == 0);
            assert(mem64.read(0) == 0xAB);

            mem64.reset();
            assert(mem64.peek(0) == 0);
        }

        // 6. The very top of a full 64-bit address space (memory_size ==
        // UINT64_MAX, the largest value representable) must not let a
        // multi-byte access silently wrap past the end of the uint64_t
        // range and alias low addresses -- it must be rejected as an
        // out-of-bounds access instead.
        {
            Config edge_cfg = create_mock_config(UINT64_MAX, 32, {}, 64);
            Memory edge_mem(edge_cfg);

            assert(edge_mem.size() == UINT64_MAX);

            edge_mem.write(0, 0x11223344);

            // A 4-byte write starting at UINT64_MAX-1 would touch
            // UINT64_MAX, then wrap to 0 and 1 -- must throw, not alias.
            try {
                edge_mem.write(UINT64_MAX - 1, 0xDEADBEEF);
                assert(false);
            } catch (const std::runtime_error &e) {
                // Expected: rejected as out of bounds.
            }

            // The rejected wraparound write must not have touched address 0.
            assert(edge_mem.read(0) == 0x11223344);
        }

        std::cout << "Memory unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
