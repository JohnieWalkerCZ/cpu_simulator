#include "register_file.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

RegisterFile::RegisterFile(const Config &config)
    : pc_index_(-1), sp_index_(-1), flags_index_(-1) {
    defs_ = config.registers;

    int max_phys_idx = -1;
    for (const auto &def : defs_) {
        if (def.physical_index > max_phys_idx) {
            max_phys_idx = def.physical_index;
        }
    }

    registers_.resize(max_phys_idx + 1, 0);

    for (size_t i = 0; i < defs_.size(); ++i) {
        name_to_index_[defs_[i].name] = i;

        if (defs_[i].role == "program_counter") {
            pc_index_ = i;
        } else if (defs_[i].role == "stack_pointer") {
            sp_index_ = i;
        } else if (defs_[i].role == "status_flags") {
            flags_index_ = i;
        }
    }
    reset();
}

uint64_t RegisterFile::read(const std::string &name) const {
    auto it = name_to_index_.find(name);
    if (it == name_to_index_.end()) {
        throw std::runtime_error("Unknown register: " + name);
    }
    return read(it->second);
}

void RegisterFile::write(const std::string &name, uint64_t value) {
    auto it = name_to_index_.find(name);
    if (it == name_to_index_.end()) {
        throw std::runtime_error("Unknown register: " + name);
    }
    write(it->second, value);
}

uint64_t RegisterFile::read(size_t index) const {
    if (index >= defs_.size()) {
        throw std::runtime_error("Register index out of bound: " +
                                 std::to_string(index));
    }
    const auto &def = defs_[index];
    uint64_t result = 0;

    for (size_t i = 0; i < def.bit_mapping.size(); ++i) {
        int phys_bit = def.bit_mapping[i];
        uint64_t bit_val = (registers_[def.physical_index] >> phys_bit) & 1;
        result |= (bit_val << i);
    }
    return result;
}

void RegisterFile::write(size_t index, uint64_t value) {
    if (index >= defs_.size()) {
        throw std::runtime_error("Register index out of bound: " +
                                 std::to_string(index));
    }
    const auto &def = defs_[index];
    uint64_t physical_val = registers_[def.physical_index];

    for (size_t i = 0; i < def.bit_mapping.size(); ++i) {
        int phys_bit = def.bit_mapping[i];
        uint64_t bit_val = (value >> i) & 1;

        physical_val &= ~(1ULL << phys_bit);
        physical_val |= (bit_val << phys_bit);
    }

    registers_[def.physical_index] = physical_val;
}

uint64_t RegisterFile::get_pc() const {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    return read(pc_index_);
}

void RegisterFile::set_pc(uint64_t value) {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    write(pc_index_, value);
}

void RegisterFile::increment_pc(int amount) {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    uint64_t current = read(pc_index_);
    uint64_t max_value = (defs_[pc_index_].width >= 64)
                             ? UINT64_MAX
                             : (1ULL << defs_[pc_index_].width) - 1;
    uint64_t new_val = (current + amount) & max_value;
    write(pc_index_, new_val);
}

void RegisterFile::set_sp(uint64_t value) {
    if (sp_index_ == -1) {
        throw std::runtime_error("No stack pointer register defined");
    }
    write(sp_index_, value);
}

std::vector<uint64_t> RegisterFile::get_all_values() const {
    std::vector<uint64_t> values(defs_.size());
    for (size_t i = 0; i < defs_.size(); ++i) {
        values[i] = read(i);
    }
    return values;
}

int RegisterFile::find_by_role(const std::string &role) const {
    for (size_t i = 0; i < defs_.size(); ++i) {
        if (defs_[i].role == role)
            return i;
    }
    return -1;
}

void RegisterFile::reset() {
    std::fill(registers_.begin(), registers_.end(), 0);

    for (const auto &def : defs_) {
        if (def.initial != 0) {
            for (size_t i = 0; i < def.bit_mapping.size(); ++i) {
                int phys_bit = def.bit_mapping[i];
                uint64_t bit_val = (def.initial >> i) & 1;

                registers_[def.physical_index] &= ~(1ULL << phys_bit);
                registers_[def.physical_index] |= (bit_val << phys_bit);
            }
        }
    }
}

uint64_t RegisterFile::mask_value(uint64_t value, int width) const {
    if (width >= 64)
        return value;
    uint64_t mask = (1ULL << width) - 1;
    return mask & value;
}
