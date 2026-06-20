#pragma once
#include "../config.hpp"
#include "../wide_int.hpp"
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class CPU;
class ExprParser;

class DeclarativePeripheral {
    friend class ExprParser;

  public:
    DeclarativePeripheral(CPU &cpu, const PeripheralDef &def);

    void reset();
    void tick();
    word_t read(word_t offset);
    void write(word_t offset, word_t value);

    void set_host_print_hook(std::function<void(char)> hook) {
        host_print_ = hook;
    }
    void set_host_pop_hook(std::function<char()> hook) { host_pop_ = hook; }

    word_t evaluate_expr(const std::string &expr, word_t context_value);
    std::string get_name() { return def_.name; };
    const std::unordered_map<std::string, word_t> &get_registers() const {
        return registers_;
    }
    const std::unordered_map<std::string, word_t> &get_internal_vars() const {
        return internal_vars_;
    }

    word_t get_start_address() const { return def_.address_start; }
    word_t get_end_address() const { return def_.address_end; }

  private:
    CPU &cpu_;
    PeripheralDef def_;

    std::unordered_map<std::string, word_t> registers_;
    std::unordered_map<std::string, word_t> internal_vars_;
    std::unordered_map<word_t, PeripheralRegisterDef, WordHash> reg_map_;

    std::function<void(char)> host_print_;
    std::function<char()> host_pop_;

    void execute_ast(const nlohmann::json &ast, word_t context_value);
    word_t get_var(const std::string &name, word_t ctx);
};
