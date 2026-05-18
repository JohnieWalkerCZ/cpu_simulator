#include "register_file.hpp"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

RegisterFile::RegisterFile(const Config &config)
    : pc_index_(-1), sp_index_(-1), flags_index_(-1) {
    defs_ = config.registers;

    registers_.resize(defs_.size());
    for (size_t i = 0; i < defs_.size(); ++i) {
        name_to_index_[defs_[i].name] = i;
        registers_[i] = defs_[i].initial;

        if (defs_[i].role == "program_counter") {
            pc_index_ = i;
        } else if (defs_[i].role == "stack_pointer") {
            sp_index_ = i;
        } else if (defs_[i].role == "status_flags") {
            flags_index_ = i;
        }
    }
}

uint64_t RegisterFile::read(const std::string &name) const {
    auto it = name_to_index_.find(name);
    if (it == name_to_index_.end()) {
        throw std::runtime_error("Unknown register: " + name);
    }
    return registers_[it->second];
}

void RegisterFile::write(const std::string &name, uint64_t value) {
    auto it = name_to_index_.find(name);
    if (it == name_to_index_.end()) {
        throw std::runtime_error("Unknown register: " + name);
    }
    size_t idx = it->second;
    registers_[idx] = mask_value(value, defs_[idx].width);
}

uint64_t RegisterFile::read(size_t index) const {
    if (index >= registers_.size()) {
        throw std::runtime_error("Register index out of bound: " +
                                 std::to_string(index));
    }

    return registers_[index];
}

void RegisterFile::write(size_t index, uint64_t value) {
    if (index >= registers_.size()) {
        throw std::runtime_error("Register index out of bound: " +
                                 std::to_string(index));
    }

    registers_[index] = mask_value(value, defs_[index].width);
}

uint64_t RegisterFile::get_pc() const {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    return registers_[pc_index_];
}

void RegisterFile::set_pc(uint64_t value) {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    registers_[pc_index_] = mask_value(value, defs_[pc_index_].width);
}

void RegisterFile::increment_pc(int amount) {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    uint64_t current = registers_[pc_index_];
    uint64_t max_value = (defs_[pc_index_].width == 64)
                             ? UINT64_MAX
                             : (1ULL << defs_[pc_index_].width) - 1;
    uint64_t new_val = (current + amount) % max_value;
    registers_[pc_index_] = new_val;
}

std::vector<uint64_t> RegisterFile::get_all_values() const {
    return registers_;
}

int RegisterFile::find_by_role(const std::string &role) const {
    for (size_t i = 0; i < registers_.size(); ++i) {
        if (defs_[i].role == role)
            return i;
    }
    return -1;
}

void RegisterFile::reset() {
    for (size_t i = 0; i < registers_.size(); ++i) {
        registers_[i] = defs_[i].initial;
    }
}

uint64_t RegisterFile::mask_value(uint64_t value, int width) const {
    if (width >= 64)
        return value;
    uint64_t mask = (1ULL << width) - 1;
    return mask & value;
}
