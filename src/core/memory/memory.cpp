
#include "memory.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
Memory::Memory(size_t size, int word_size_bits)
    : memory_(size, 0), word_size_bytes_((word_size_bits + 7) / 8) {
    if (word_size_bits != 4 && word_size_bits != 8 && word_size_bits != 16 &&
        word_size_bits != 32 && word_size_bits != 64) {
        throw std::runtime_error("Unsupported word size: " +
                                 std::to_string(word_size_bits) + " bits");
    }

    if (word_size_bytes_ == 0) word_size_bytes_ = 1;

    if (word_size_bytes_ == 8) {
        mask_ = UINT64_MAX;
    } else {
        mask_ = (1ULL << word_size_bits) - 1;
    }
}

uint64_t Memory::read(uint32_t address) const {
    if (!is_valid_address(address)) {
        throw std::runtime_error("Memory read out of bounds: " +
                                 std::to_string(address));
    }

    uint64_t result = 0;
    for (int i = 0; i < word_size_bytes_; ++i) {
        result |= (static_cast<uint64_t>(memory_[address + i])) << (i * 8);
    }

    return result;
}

void Memory::write(uint32_t address, uint64_t value) {
    if (!is_valid_address(address)) {
        throw std::runtime_error("Memory read out of bounds: " +
                                 std::to_string(address));
    }
    value &= mask_;
    for (int i = 0; i < word_size_bytes_; ++i) {
        memory_[address + i] = static_cast<uint8_t>((value >> (i * 8) & 0xFF));
    }
}

std::vector<uint8_t> Memory::read_bytes(uint32_t address, size_t count) const {
    std::vector<uint8_t> result;
    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        if (!is_valid_address(address + i)) {
            throw std::runtime_error("Memory read out of bound");
        }
        result.push_back(memory_[address + i]);
    }
    return result;
}

void Memory::write_bytes(uint32_t address, const std::vector<uint8_t> &data) {
    for (size_t i = 0; i < data.size(); ++i) {
        if (!is_valid_address(address + i)) {
            throw std::runtime_error("Memory write out of bounds");
        }
        memory_[address + i] = data[i];
    }
}

void Memory::load_program(const std::vector<uint8_t> &machine_code,
                          uint32_t start_address) {
    write_bytes(start_address, machine_code);
}

bool Memory::is_valid_address(uint32_t address) const {
    return address + word_size_bytes_ - 1 < memory_.size();
}

void Memory::reset() { std::fill(memory_.begin(), memory_.end(), 0); }
