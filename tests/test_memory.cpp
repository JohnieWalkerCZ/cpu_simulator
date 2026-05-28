#include "core/memory/memory.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        // 1. Test memory with 8-bit word size
        Memory mem8(1024, 8);
        assert(mem8.size() == 1024);
        assert(mem8.word_size_bytes() == 1);

        // Write and read
        mem8.write(10, 0xAB);
        assert(mem8.read(10) == 0xAB);

        // Value masking (8-bit)
        mem8.write(11, 0x1234);
        assert(mem8.read(11) == 0x34); // masked to 8-bit

        // 2. Test memory with 16-bit word size
        Memory mem16(1024, 16);
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
        Memory mem4(64, 4);
        assert(mem4.word_size_bytes() == 1);
        mem4.write(5, 0xFE);
        assert(mem4.read(5) == 0x0E); // Masked to 4-bit (0x0F is max)

        // 4. Bounds checking
        try {
            mem8.read(1024);
            assert(false);
        } catch (const std::runtime_error &e) {
            // Expected
        }

        try {
            mem8.write(1024, 0);
            assert(false);
        } catch (const std::runtime_error &e) {
            // Expected
        }

        // 16-bit memory bounds check (since word size is 2, address 1023 reads indices 1023 and 1024, which is out of bounds)
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
        Memory mem_io(256, 8);
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
            }
        );

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

        std::cout << "Memory unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
