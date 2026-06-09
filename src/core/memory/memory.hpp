#pragma once
#include "../config.hpp"
#include <cstdint>
#include <functional>
#include <vector>

using MMIO_ReadCallback = std::function<uint64_t(uint64_t)>;
using MMIO_WriteCallback = std::function<void(uint64_t, uint64_t)>;

enum class Endianness { Little, Big };
enum class MemoryArch { VonNeumann, Harvard };

struct MMIORegion {
    uint64_t start;
    uint64_t end;
    MMIO_ReadCallback read_cb;
    MMIO_WriteCallback write_cb;
};

class Memory {
  public:
    Memory(const Config &cfg);

    uint64_t read(uint64_t address, bool is_execute = false,
                  int width_bits = 0) const;
    void write(uint64_t address, uint64_t value, int width_bits = 0);

    std::vector<uint8_t> read_bytes(uint64_t address, size_t count) const;
    void write_bytes(uint64_t address, const std::vector<uint8_t> &data);

    void load_program(const std::vector<uint8_t> &machine_code,
                      uint64_t start_address);

    size_t size() const { return memory_.size(); }
    const std::vector<uint8_t> &raw() const { return memory_; }
    std::vector<uint8_t> &raw() { return memory_; }

    bool is_valid_address(uint64_t address) const;

    void reset();

    int word_size_bytes() const { return word_size_bytes_; }

    void map_io_region(uint64_t start, uint64_t end, MMIO_ReadCallback r_cb,
                       MMIO_WriteCallback w_cb);
    void reset_io_hooks();

    uint64_t port_read(uint64_t port);
    void port_write(uint64_t port, uint64_t value);
    void map_port_region(uint64_t start, uint64_t end, MMIO_ReadCallback r_cb,
                         MMIO_WriteCallback w_cb);
    const std::vector<uint8_t> &raw_instruction() const {
        return instruction_memory_;
    };
    bool is_harvard() const { return arch_ == MemoryArch::Harvard; };

  private:
    Endianness endianness_;
    MemoryArch arch_;

    std::vector<uint8_t> memory_;
    std::vector<uint8_t> instruction_memory_;

    int word_size_bytes_;
    uint64_t mask_;

    std::vector<MMIORegion> io_regions_;
    std::vector<MMIORegion> port_regions_;
    std::vector<MemorySegmentDef> segments_;

    const MMIORegion *find_io_region(uint64_t address) const;
    void check_access(uint64_t address, bool reg_r, bool reg_w,
                      bool reg_x) const;
};
