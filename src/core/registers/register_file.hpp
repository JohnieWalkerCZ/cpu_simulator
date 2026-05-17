#pragma once
#include "../config.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class RegisterFile {
  public:
    RegisterFile(const Config &config);

    uint64_t read(const std::string &name) const;
    void write(const std::string &name, uint64_t value);

    uint64_t read(size_t index) const;
    void write(size_t index, uint64_t value);

    uint64_t get_pc() const;
    void set_pc(uint64_t value);
    void increment_pc(int amount = 1);

    size_t size() const { return registers_.size(); }
    const std::vector<RegisterDef> &get_defs() const { return defs_; }
    std::vector<uint64_t> get_all_values() const;

    int find_by_role(const std::string &role) const;

    void reset();

  private:
    std::vector<RegisterDef> defs_;
    std::vector<uint64_t> registers_;
    std::unordered_map<std::string, size_t> name_to_index_;
    int pc_index_;
    int sp_index_;
    int flags_index_;

    uint64_t mask_value(uint64_t value, int width) const;
};
