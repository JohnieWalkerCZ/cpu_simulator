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
      segments_(config.memory_segments),
      endianness_(config.endianness == "big" ? Endianness::Big
                                             : Endianness::Little),
      arch_(config.memory_architecture == "harvard" ? MemoryArch::Harvard
                                                    : MemoryArch::VonNeumann) {

    if (config.data_width != 4 && config.data_width != 8 &&
        config.data_width != 16 && config.data_width != 32 &&
        config.data_width != 64) {
        throw std::runtime_error("Unsupported word size.");
    }

    if (arch_ == MemoryArch::Harvard) {
        instruction_memory_.resize(config.memory_size, 0);
    }

    if (word_size_bytes_ == 0)
        word_size_bytes_ = 1;
    if (word_size_bytes_ == 8)
        mask_ = UINT64_MAX;
    else
        mask_ = (1ULL << config.data_width) - 1;
}

void Memory::check_access(uint64_t address, bool req_r, bool req_w,
                          bool req_x) const {
    if (!is_valid_address(address)) {
        throw std::runtime_error("Memory Access Violation: Address 0x" +
                                 std::to_string(address) +
                                 " is out of physical bounds.");
    }

    if (req_w && arch_ == MemoryArch::Harvard) {
        return;
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

uint64_t Memory::read(uint64_t address, bool is_execute, int width_bits) const {
    if (auto reg = find_io_region(address)) {
        return reg->read_cb ? reg->read_cb(address) : 0;
    }

    int target_width = (width_bits > 0) ? width_bits : (word_size_bytes_ * 8);
    int target_bytes = (target_width + 7) / 8;

    for (int i = 0; i < target_bytes; ++i) {
        check_access(address + i, !is_execute, false, is_execute);
    }

    uint64_t result = 0;
    const std::vector<uint8_t> &target_array =
        (arch_ == MemoryArch::Harvard && is_execute) ? instruction_memory_
                                                     : memory_;

    for (int i = 0; i < target_bytes; ++i) {
        int shift = (endianness_ == Endianness::Little)
                        ? (i * 8)
                        : ((target_bytes - 1 - i) * 8);
        result |= (static_cast<uint64_t>(target_array[address + i])) << shift;
    }
    return result;
}

void Memory::write(uint64_t address, uint64_t value, int width_bits) {
    if (auto reg = find_io_region(address)) {
        if (reg->write_cb)
            reg->write_cb(address, value);
        return;
    }

    int target_width = (width_bits > 0) ? width_bits : (word_size_bytes_ * 8);
    int target_bytes = (target_width + 7) / 8;

    for (int i = 0; i < target_bytes; ++i) {
        check_access(address + i, false, true, false);
    }

    uint64_t mask =
        (target_width >= 64) ? UINT64_MAX : (1ULL << target_width) - 1;
    value &= mask;

    for (int i = 0; i < target_bytes; ++i) {
        int shift = (endianness_ == Endianness::Little)
                        ? (i * 8)
                        : ((target_bytes - 1 - i) * 8);
        memory_[address + i] = static_cast<uint8_t>((value >> shift) & 0xFF);
    }
}

std::vector<uint8_t> Memory::read_bytes(uint64_t address, size_t count) const {
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

void Memory::write_bytes(uint64_t address, const std::vector<uint8_t> &data) {
    for (size_t i = 0; i < data.size(); ++i) {
        if (!is_valid_address(address + i)) {
            throw std::runtime_error("Memory write out of bounds");
        }
        if (arch_ == MemoryArch::Harvard) {
            instruction_memory_[address + i] = data[i];
        } else {
            memory_[address + i] = data[i];
        }
    }
}

void Memory::load_program(const std::vector<uint8_t> &machine_code,
                          uint64_t start_address) {
    write_bytes(start_address, machine_code);
}

bool Memory::is_valid_address(uint64_t address) const {
    return address + word_size_bytes_ - 1 < memory_.size();
}

void Memory::reset() { std::fill(memory_.begin(), memory_.end(), 0); }

void Memory::map_io_region(uint64_t start, uint64_t end, MMIO_ReadCallback r_cb,
                           MMIO_WriteCallback w_cb) {
    io_regions_.push_back({start, end, r_cb, w_cb});
}

void Memory::reset_io_hooks() { io_regions_.clear(); }

const MMIORegion *Memory::find_io_region(uint64_t address) const {
    for (const MMIORegion &r : io_regions_) {
        if (address >= r.start && address <= r.end) {
            return &r;
        }
    }

    return nullptr;
}

void Memory::map_port_region(uint64_t start, uint64_t end,
                             MMIO_ReadCallback r_cb, MMIO_WriteCallback w_cb) {
    port_regions_.push_back({start, end, r_cb, w_cb});
}

uint64_t Memory::port_read(uint64_t port) {
    for (const auto &r : port_regions_) {
        if (port >= r.start && port <= r.end) {
            return r.read_cb ? r.read_cb(port) : 0;
        }
    }
    return 0;
}

void Memory::port_write(uint64_t port, uint64_t value) {
    for (const auto &r : port_regions_) {
        if (port >= r.start && port <= r.end) {
            if (r.write_cb)
                r.write_cb(port, value);
            return;
        }
    }
}
