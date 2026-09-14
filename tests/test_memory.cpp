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

        // 7. Regression: a single-byte access at the very top of memory must
        // succeed even when the architecture's native word is wider than a
        // byte (is_valid_address used to bounds-check against the fixed
        // word size instead of the actual access width, rejecting legal
        // byte accesses near the end of memory).
        {
            Config wide_cfg = create_mock_config(65536, 16, {}, 16);
            Memory wide_mem(wide_cfg);

            wide_mem.write(65535, 0xAB, /*width_bits=*/8);
            assert(wide_mem.read(65535, false, /*width_bits=*/8) == 0xAB);

            // A full 16-bit (2-byte) access that actually runs past the end
            // of memory must still be rejected.
            try {
                wide_mem.write(65535, 0x1234, /*width_bits=*/16);
                assert(false);
            } catch (const std::runtime_error &e) {
                // Expected: out of bounds.
            }
        }

        // 8. Harvard architecture: instruction memory and data memory are
        // physically separate page tables. A byte loaded as code (via
        // write_bytes, as CPU::load_program does) must be visible to
        // fetches (read(addr, /*is_execute=*/true)) but invisible to plain
        // data reads, and vice versa for a byte written as data. This
        // documented behavior (CONFIGURATION.md 2.1) had no test coverage.
        {
            Config harvard_cfg = create_mock_config(256, 8, {}, 8);
            harvard_cfg.memory_architecture = "harvard";
            Memory hmem(harvard_cfg);

            hmem.write_bytes(0x10, {0xAB}); // loads into instruction space
            assert(hmem.read(0x10, /*is_execute=*/true) == 0xAB);
            assert(hmem.read(0x10, /*is_execute=*/false) == 0);

            hmem.write(0x20, 0xCD); // plain write always targets data space
            assert(hmem.read(0x20, /*is_execute=*/false) == 0xCD);
            assert(hmem.read(0x20, /*is_execute=*/true) == 0);
        }

        // 9. Port I/O regions (port_read/port_write) are a separate address
        // space from memory-mapped I/O and bypass segment permission
        // checks entirely -- used by the "port_read"/"port_write" microcode
        // actions. No test previously exercised map_port_region at all.
        {
            Config cfg = create_mock_config(256, 8, {}, 8);
            Memory mem(cfg);

            word_t last_written_port = 0, last_written_val = 0;
            mem.map_port_region(
                0x00, 0x0F,
                [](word_t port) -> word_t { return port * 10; },
                [&](word_t port, word_t val) {
                    last_written_port = port;
                    last_written_val = val;
                });

            assert(mem.port_read(0x05) == 50);
            mem.port_write(0x05, 42);
            assert(last_written_port == 0x05 && last_written_val == 42);

            // An unmapped port must not throw -- reads default to 0, writes
            // are silently dropped.
            assert(mem.port_read(0x50) == 0);
            mem.port_write(0x50, 99); // must not throw
        }

        std::cout << "Memory unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
