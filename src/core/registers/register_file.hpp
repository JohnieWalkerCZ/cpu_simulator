#pragma once
#include "../config.hpp"
#include "../wide_int.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class RegisterFile {
  public:
    RegisterFile(const Config &config);

    word_t read(const std::string &name) const;
    void write(const std::string &name, word_t value);

    word_t read(size_t index) const;
    void write(size_t index, word_t value);

    word_t get_pc() const;
    void set_pc(word_t value);
    void increment_pc(int amount = 1);

    void set_sp(word_t value);

    size_t size() const { return registers_.size(); }
    const std::vector<RegisterDef> &get_defs() const { return defs_; }
    std::vector<word_t> get_all_values() const;

    int find_by_role(const std::string &role) const;

    void reset();

    const std::vector<word_t> &get_physical_registers() const {
        return registers_;
    }
    void set_physical_registers(const std::vector<word_t> &state) {
        registers_ = state;
    }

    word_t read_coproc(int cp_id, int reg_id) const;
    void write_coproc(int cp_id, int reg_id, word_t value);

  private:
    std::vector<RegisterDef> defs_;
    std::vector<word_t> registers_;
    std::unordered_map<std::string, size_t> name_to_index_;
    int pc_index_;
    int sp_index_;
    int flags_index_;

    word_t mask_value(word_t value, int width) const;
    void compute_contiguous_mappings();
};
