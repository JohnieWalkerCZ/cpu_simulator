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
        // 1. Test memory with 8-bit word size
        Config cfg8 = create_mock_config(1024, 8);
        Memory mem8(cfg8);
        assert(mem8.size() == 1024);
        assert(mem8.word_size_bytes() == 1);

        // Write and read
        mem8.write(10, 0xAB);
        assert(mem8.read(10) == 0xAB);

        // Value masking (8-bit)
        mem8.write(11, 0x1234);
        assert(mem8.read(11) == 0x34); // masked to 8-bit

        // 2. Test memory with 16-bit word size
        Config cfg16 = create_mock_config(1024, 16);
        Memory mem16(cfg16);
        assert(mem16.word_size_bytes() == 2);

        // Write and read (multi-byte layout check)
        mem16.write(10, 0xABCD);
        assert(mem16.read(10) == 0xABCD);
        // Verify underlying byte storage (little endian)
        assert(mem16.raw()[10] == 0xCD);
        assert(mem16.raw()[11] == 0xAB);

        // Masking (16-bit)
        mem16.write(12, 0x12345678ULL);
        assert(mem16.read(12) == 0x5678ULL);

        // 3. Test memory with 4-bit word size
        Config cfg4 = create_mock_config(64, 4);
        Memory mem4(cfg4);
        assert(mem4.word_size_bytes() == 1);
        mem4.write(5, 0xFE);
        assert(mem4.read(5) == 0x0E); // Masked to 4-bit (0x0F is max)

        // 4. Bounds checking
        try {
            mem8.read(1024);
            assert(false);
        } catch (const std::runtime_error &e) {
            // Expected out of physical memory range
        }

        try {
            mem8.write(1024, 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            // Expected out of physical memory range
        }

        // 16-bit memory bounds check (reads indices 1023 and 1024, which is out
        // of bounds)
        try {
            mem16.read(1023);
            assert(false);
        } catch (const std::runtime_error &e) {
            // Expected
        }

        // 5. Test loading programs and bytes operations
        std::vector<uint8_t> prog = {0x01, 0x02, 0x03, 0x04};
        mem8.load_program(prog, 100);
        auto read_back = mem8.read_bytes(100, 4);
        assert(read_back == prog);

        // Test out of bounds read_bytes
        try {
            mem8.read_bytes(1022, 4);
            assert(false);
        } catch (const std::runtime_error &e) {
            // Expected
        }

        // 6. Test MMIO mapping and callbacks
        Config cfg_io = create_mock_config(256, 8);
        Memory mem_io(cfg_io);
        bool read_triggered = false;
        bool write_triggered = false;
        uint64_t written_val = 0;

        mem_io.map_io_region(
            0x80, 0x8F,
            [&read_triggered](uint32_t addr) -> uint64_t {
                read_triggered = true;
                return 0x55;
            },
            [&write_triggered, &written_val](uint32_t addr, uint64_t val) {
                write_triggered = true;
                written_val = val;
            });

        // Test read MMIO
        uint64_t val = mem_io.read(0x85);
        assert(read_triggered);
        assert(val == 0x55);

        // Test write MMIO
        mem_io.write(0x8A, 0x99);
        assert(write_triggered);
        assert(written_val == 0x99);

        // Reset IO hooks
        mem_io.reset_io_hooks();
        read_triggered = false;
        write_triggered = false;
        mem_io.write(0x85, 0x12);
        assert(!write_triggered);
        assert(mem_io.read(0x85) == 0x12);

        // 7. NEW: Test Memory segment access rights and protections
        std::vector<MemorySegmentDef> protected_segments = {
            {"ROM", 0x0000, 0x00FF, true, false, true},  // R=1, W=0, X=1
            {"RAM", 0x0100, 0x01FF, true, true, false},  // R=1, W=1, X=0
            {"STACK", 0x0200, 0x02FF, true, true, false} // R=1, W=1, X=0
        };
        Config cfg_prot = create_mock_config(1024, 8, protected_segments);
        Memory mem_prot(cfg_prot);

        // Test ROM Protection: Write to ROM must throw an access violation
        try {
            mem_prot.write(0x0050, 0xAA);
            assert(false); // Should have thrown
        } catch (const std::runtime_error &e) {
            // Expected: Write violation
        }

        // Test ROM Execution: Read with is_execute=true must pass
        try {
            mem_prot.read(0x0050, true);
        } catch (const std::exception &e) {
            assert(false); // Execution should be permitted
        }

        // Test RAM Protection: Execute from RAM must throw an access violation
        try {
            mem_prot.read(0x0150, true); // is_execute = true
            assert(false);               // Should have thrown
        } catch (const std::runtime_error &e) {
            // Expected: Execute violation
        }

        // Test RAM Read/Write: Standard memory loads and stores must pass
        try {
            mem_prot.write(0x0150, 0x55);
            assert(mem_prot.read(0x0150, false) == 0x55); // is_execute = false
        } catch (const std::exception &e) {
            assert(false); // Standard read/write should be permitted
        }

        // Test Unmapped Address Protection: Access outside valid bounds must
        // throw
        try {
            mem_prot.read(
                0x0350);   // Address lies outside ROM, RAM, or STACK boundaries
            assert(false); // Should have thrown
        } catch (const std::runtime_error &e) {
            // Expected: Unmapped area fault
        }

        std::cout << "Memory unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
