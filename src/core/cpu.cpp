#include "cpu.hpp"
#include <cstdint>
#include <vector>

CPU::CPU(Config &cfg)
    : cfg_(cfg), regs_(cfg), mem_(cfg.memory_size, cfg.data_width), alu_(cfg),
      executor_(cfg, regs_, mem_, alu_), code_(std::vector<uint8_t>()),
      load_address_(0) {
    reset();
}

void CPU::load_program(const std::vector<uint8_t> &code, uint32_t address) {
    code_ = code;
    load_address_ = address;

    mem_.load_program(code, address);
}

void CPU::step() { executor_.step_instruction(); }

void CPU::step_uop() { executor_.step_uop(); }

void CPU::run(int max_cycles) {
    int count = 0;
    while (!executor_.is_halted() && count < max_cycles) {
        executor_.step_instruction();
        count++;
    }
}

void CPU::reset() {
    regs_.reset();
    mem_.reset();

    executor_.reset();

    if (!code_.empty()) {
        mem_.load_program(code_, load_address_);
        regs_.set_pc(load_address_);
    }
}
