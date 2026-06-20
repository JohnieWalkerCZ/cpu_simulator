#pragma once
#include "alu/alu.hpp"
#include "config.hpp"
#include "execution/executor.hpp"
#include "memory/memory.hpp"
#include "peripherals/declarative_peripheral.hpp"
#include "registers/register_file.hpp"
#include "wide_int.hpp"
#include <cstdint>
#include <vector>

class CPU {
  public:
    explicit CPU(Config &cfg);

    void load_program(const std::vector<uint8_t> &code, word_t address = 0);
    void step();
    void step_uop();
    void run(int max_cycles = 10000);
    void reset();

    RegisterFile &get_registers() { return regs_; }
    Memory &get_memory() { return mem_; }
    ALU &get_alu() { return alu_; }
    Executor &get_executor() { return executor_; }
    const Executor &get_executor() const { return executor_; }

    const Config &get_config() const { return cfg_; }

    bool is_halted() const { return executor_.is_halted(); }
    const std::vector<uint8_t> &get_code() const { return code_; }
    word_t get_load_address() const { return load_address_; }

    std::vector<DeclarativePeripheral> &get_peripherals() {
        return dec_peripherals_;
    }
    void trigger_interrupt(int interrupt_id);

  private:
    Config &cfg_;
    RegisterFile regs_;
    Memory mem_;
    ALU alu_;
    Executor executor_;

    std::vector<uint8_t> code_;
    word_t load_address_;
    std::vector<DeclarativePeripheral> dec_peripherals_;
};
