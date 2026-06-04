#include "memory.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
Memory::Memory(const Config &config)
    : memory_(config.memory_size, 0),
      word_size_bytes_((config.data_width + 7) / 8),
      segments_(config.memory_segments) {

    if (config.data_width != 4 && config.data_width != 8 &&
        config.data_width != 16 && config.data_width != 32 &&
        config.data_width != 64) {
        throw std::runtime_error("Unsupported word size.");
    }

    if (word_size_bytes_ == 0)
        word_size_bytes_ = 1;
    if (word_size_bytes_ == 8)
        mask_ = UINT64_MAX;
    else
        mask_ = (1ULL << config.data_width) - 1;
}

void Memory::check_access(uint32_t address, bool req_r, bool req_w,
                          bool req_x) const {
    if (!is_valid_address(address)) {
        throw std::runtime_error("Memory Access Violation: Address 0x" +
                                 std::to_string(address) +
                                 " is out of physical bounds.");
    }

    for (const auto &seg : segments_) {
        if (address >= seg.start && address <= seg.end) {
            if (req_r && !seg.r)
                throw std::runtime_error(
                    "Memory Protection Fault: Read violation at 0x" +
                    std::to_string(address));
            if (req_w && !seg.w)
                throw std::runtime_error(
                    "Memory Protection Fault: Write violation at 0x" +
                    std::to_string(address));
            if (req_x && !seg.x)
                throw std::runtime_error(
                    "Memory Protection Fault: Execution violation at 0x" +
                    std::to_string(address));
            return;
        }
    }
    throw std::runtime_error("Memory Protection Fault: Address 0x" +
                             std::to_string(address) + " is unmapped.");
}

uint64_t Memory::read(uint32_t address, bool is_execute) const {
    if (auto reg = find_io_region(address)) {
        return reg->read_cb ? reg->read_cb(address) : 0;
    }

    check_access(address, !is_execute, false, is_execute);

    uint64_t result = 0;
    for (int i = 0; i < word_size_bytes_; ++i) {
        result |= (static_cast<uint64_t>(memory_[address + i])) << (i * 8);
    }
    return result;
}

void Memory::write(uint32_t address, uint64_t value) {
    if (auto reg = find_io_region(address)) {
        if (reg->write_cb)
            reg->write_cb(address, value);
        return;
    }

    check_access(address, false, true, false);

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

void Memory::map_io_region(uint32_t start, uint32_t end, MMIO_ReadCallback r_cb,
                           MMIO_WriteCallback w_cb) {
    io_regions_.push_back({start, end, r_cb, w_cb});
}

void Memory::reset_io_hooks() { io_regions_.clear(); }

const MMIORegion *Memory::find_io_region(uint32_t address) const {
    for (const MMIORegion &r : io_regions_) {
        if (address >= r.start && address <= r.end) {
            return &r;
        }
    }

    return nullptr;
}
