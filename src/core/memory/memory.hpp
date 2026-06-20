#pragma once
#include "../config.hpp"
#include "../wide_int.hpp"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

using MMIO_ReadCallback = std::function<word_t(word_t)>;
using MMIO_WriteCallback = std::function<void(word_t, word_t)>;

enum class Endianness { Little, Big };
enum class MemoryArch { VonNeumann, Harvard };

struct MMIORegion {
    word_t start;
    word_t end;
    MMIO_ReadCallback read_cb;
    MMIO_WriteCallback write_cb;
};

class Memory {
  public:
    static constexpr uint64_t PAGE_SIZE = 65536; // 64 KB pages

    // Address space is backed by lazily-allocated pages rather than one
    // contiguous buffer, so a wide (up to 128-bit) address bus with e.g. 4GB
    // of configured memory doesn't eagerly allocate anything at construction
    // time -- only pages that are actually written ever get a backing
    // buffer. Pages are keyed by word_t since addr_width can exceed 64 bits.
    using PageTable = std::unordered_map<word_t, std::vector<uint8_t>, WordHash>;

    // Snapshot of only the pages that have been touched. Cheap to copy even
    // for huge address spaces, unlike a full contiguous copy of the
    // configured address space would be.
    struct Snapshot {
        PageTable pages;
        PageTable instruction_pages;
    };

    Memory(const Config &cfg);

    word_t read(word_t address, bool is_execute = false,
               int width_bits = 0) const;
    void write(word_t address, word_t value, int width_bits = 0);

    // Single-byte access bypassing protection checks, for UI/debug display
    // only. Returns 0 for addresses whose page was never written.
    uint8_t peek(word_t address) const;
    uint8_t peek_instruction(word_t address) const;

    std::vector<uint8_t> read_bytes(word_t address, size_t count) const;
    void write_bytes(word_t address, const std::vector<uint8_t> &data);

    void load_program(const std::vector<uint8_t> &machine_code,
                      word_t start_address);

    word_t size() const { return memory_size_; }

    bool is_valid_address(word_t address) const;

    void reset();

    int word_size_bytes() const { return word_size_bytes_; }

    void map_io_region(word_t start, word_t end, MMIO_ReadCallback r_cb,
                       MMIO_WriteCallback w_cb);
    void reset_io_hooks();

    word_t port_read(word_t port);
    void port_write(word_t port, word_t value);
    void map_port_region(word_t start, word_t end, MMIO_ReadCallback r_cb,
                         MMIO_WriteCallback w_cb);

    Snapshot capture_snapshot() const;
    void restore_snapshot(const Snapshot &snap);

    bool is_harvard() const { return arch_ == MemoryArch::Harvard; };

  private:
    Endianness endianness_;
    MemoryArch arch_;

    word_t memory_size_;
    PageTable pages_;
    PageTable instruction_pages_;

    int word_size_bytes_;
    word_t mask_;

    std::vector<MMIORegion> io_regions_;
    std::vector<MMIORegion> port_regions_;
    std::vector<MemorySegmentDef> segments_;

    const MMIORegion *find_io_region(word_t address) const;
    void check_access(word_t address, bool reg_r, bool reg_w,
                      bool reg_x) const;

    static uint8_t read_page_byte(const PageTable &pages, word_t address);
    static void write_page_byte(PageTable &pages, word_t address,
                                uint8_t value);
};
