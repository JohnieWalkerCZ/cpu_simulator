#pragma once
#include <cstdint>
#include <functional>
#include <vector>

using MMIO_ReadCallback = std::function<uint64_t(uint32_t)>;
using MMIO_WriteCallback = std::function<void(uint32_t, uint64_t)>;

struct MMIORegion {
    uint32_t start;
    uint32_t end;
    MMIO_ReadCallback read_cb;
    MMIO_WriteCallback write_cb;
};

class Memory {
  public:
    Memory(size_t size, int word_size_bits = 8);

    uint64_t read(uint32_t address) const;
    void write(uint32_t address, uint64_t value);

    std::vector<uint8_t> read_bytes(uint32_t address, size_t count) const;
    void write_bytes(uint32_t address, const std::vector<uint8_t> &data);

    void load_program(const std::vector<uint8_t> &machine_code,
                      uint32_t start_address);

    size_t size() const { return memory_.size(); }
    const std::vector<uint8_t> &raw() const { return memory_; }
    std::vector<uint8_t> &raw() { return memory_; }

    bool is_valid_address(uint32_t address) const;

    void reset();

    int word_size_bytes() const { return word_size_bytes_; }

    void map_io_region(uint32_t start, uint32_t end, MMIO_ReadCallback r_cb,
                       MMIO_WriteCallback w_cb);
    void reset_io_hooks();

  private:
    std::vector<uint8_t> memory_;
    int word_size_bytes_;
    uint64_t mask_;
    std::vector<MMIORegion> io_regions_;
    const MMIORegion *find_io_region(uint32_t address) const;
};
