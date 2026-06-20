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

        if (defs_[i].role == "program_counter" || defs_[i].role == "pc") {
            pc_index_ = i;
        } else if (defs_[i].role == "stack_pointer" || defs_[i].role == "sp") {
            sp_index_ = i;
        } else if (defs_[i].role == "status_flags" ||
                   defs_[i].role == "flags") {
            flags_index_ = i;
        }
    }
    compute_contiguous_mappings();
    reset();
}

void RegisterFile::compute_contiguous_mappings() {
    for (auto &def : defs_) {
        def.is_contiguous = false;
        def.bit_mask = 0;
        def.bit_shift = 0;

        if (def.bit_mapping.empty() || def.width <= 0 || def.width > 128)
            continue;

        int base = def.bit_mapping[0];
        if (base < 0)
            continue;

        bool contiguous = true;
        for (size_t i = 1; i < def.bit_mapping.size(); ++i) {
            if (def.bit_mapping[i] != base + static_cast<int>(i)) {
                contiguous = false;
                break;
            }
        }
        if (!contiguous)
            continue;

        if (base + def.width - 1 >= 128)
            continue; // can't fit in a 128-bit mask/shift

        def.is_contiguous = true;
        def.bit_shift = base;
        def.bit_mask = mask_for_width(def.width) << base;
    }
}

word_t RegisterFile::read(const std::string &name) const {
    auto it = name_to_index_.find(name);
    if (it == name_to_index_.end()) {
        throw std::runtime_error("Unknown register: " + name);
    }
    return read(it->second);
}

void RegisterFile::write(const std::string &name, word_t value) {
    auto it = name_to_index_.find(name);
    if (it == name_to_index_.end()) {
        throw std::runtime_error("Unknown register: " + name);
    }
    write(it->second, value);
}

word_t RegisterFile::read(size_t index) const {
    if (index >= defs_.size()) {
        throw std::runtime_error("Register index out of bound: " +
                                 std::to_string(index));
    }
    const auto &def = defs_[index];

    if (def.is_contiguous) {
        return (registers_[def.physical_index] & def.bit_mask) >>
               def.bit_shift;
    }

    word_t result = 0;
    for (size_t i = 0; i < def.bit_mapping.size(); ++i) {
        int phys_bit = def.bit_mapping[i];
        if (phys_bit == -1)
            continue;
        word_t bit_val = (registers_[def.physical_index] >> phys_bit) & 1;
        result |= (bit_val << i);
    }
    return result;
}

void RegisterFile::write(size_t index, word_t value) {
    if (index >= defs_.size()) {
        throw std::runtime_error("Register index out of bound: " +
                                 std::to_string(index));
    }
    const auto &def = defs_[index];

    if (def.is_contiguous) {
        word_t width_mask = mask_for_width(def.width);
        word_t shifted = (value & width_mask) << def.bit_shift;
        registers_[def.physical_index] =
            (registers_[def.physical_index] & ~def.bit_mask) | shifted;
        return;
    }

    word_t physical_val = registers_[def.physical_index];

    for (size_t i = 0; i < def.bit_mapping.size(); ++i) {
        int phys_bit = def.bit_mapping[i];
        if (phys_bit == -1)
            continue;
        word_t bit_val = (value >> i) & 1;

        physical_val &= ~(static_cast<word_t>(1) << phys_bit);
        physical_val |= (bit_val << phys_bit);
    }

    registers_[def.physical_index] = physical_val;
}

word_t RegisterFile::get_pc() const {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    return read(pc_index_);
}

void RegisterFile::set_pc(word_t value) {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    write(pc_index_, value);
}

void RegisterFile::increment_pc(int amount) {
    if (pc_index_ == -1) {
        throw std::runtime_error("No program counter register defined");
    }
    word_t current = read(pc_index_);
    word_t max_value = mask_for_width(defs_[pc_index_].width);
    word_t new_val;
    if (amount >= 0) {
        new_val = (current + static_cast<word_t>(amount)) & max_value;
    } else {
        new_val = (current - static_cast<word_t>(-amount)) & max_value;
    }
    write(pc_index_, new_val);
}

void RegisterFile::set_sp(word_t value) {
    if (sp_index_ == -1) {
        throw std::runtime_error("No stack pointer register defined");
    }
    write(sp_index_, value);
}

std::vector<word_t> RegisterFile::get_all_values() const {
    std::vector<word_t> values(defs_.size());
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
                if (phys_bit == -1)
                    continue;
                word_t bit_val = (def.initial >> i) & 1;

                registers_[def.physical_index] &=
                    ~(static_cast<word_t>(1) << phys_bit);
                registers_[def.physical_index] |= (bit_val << phys_bit);
            }
        }
    }
}

word_t RegisterFile::mask_value(word_t value, int width) const {
    return value & mask_for_width(width);
}

word_t RegisterFile::read_coproc(int cp_id, int reg_id) const {
    for (size_t i = 0; i < defs_.size(); ++i) {
        if (defs_[i].is_coproc && defs_[i].coproc_id == cp_id &&
            defs_[i].coproc_reg_id == reg_id) {
            return read(i);
        }
    }
    return 0;
}

void RegisterFile::write_coproc(int cp_id, int reg_id, word_t value) {
    for (size_t i = 0; i < defs_.size(); ++i) {
        if (defs_[i].is_coproc && defs_[i].coproc_id == cp_id &&
            defs_[i].coproc_reg_id == reg_id) {
            write(i, value);
            return;
        }
    }
}
