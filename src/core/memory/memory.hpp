#pragma once
#include <cstdint>
#include <vector>

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

  private:
    std::vector<uint8_t> memory_;
    int word_size_bytes_;
    uint64_t mask_;
};
