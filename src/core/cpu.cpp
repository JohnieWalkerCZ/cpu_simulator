#include "cpu.hpp"
#include "execution/executor.hpp"
#include <cstdint>
#include <vector>

CPU::CPU(Config &cfg)
    : cfg_(cfg), regs_(cfg), mem_(cfg), alu_(cfg),
      executor_(cfg, regs_, mem_, alu_), code_(std::vector<uint8_t>()),
      load_address_(0) {

    for (const auto &p_def : cfg.peripherals) {
        if (p_def.type == "declarative") {
            dec_peripherals_.emplace_back(*this, p_def);
        }
    }

    for (auto &dp : dec_peripherals_) {
        mem_.map_io_region(
            dp.get_start_address(), dp.get_end_address(),
            [&dp, start = dp.get_start_address()](uint32_t addr) {
                return dp.read(addr - start);
            },
            [&dp, start = dp.get_start_address()](uint32_t addr, uint64_t val) {
                dp.write(addr - start, val);
            });
    }

    reset();
}

void CPU::load_program(const std::vector<uint8_t> &code, uint32_t address) {
    code_ = code;
    load_address_ = address;

    mem_.load_program(code, address);
}

void CPU::step() {
    do {
        step_uop();
    } while (executor_.get_state() != ExecutionState::FETCH && !is_halted());
}

void CPU::step_uop() {
    executor_.step_uop();
    for (auto &p : dec_peripherals_) {
        p.tick();
    }
}

void CPU::run(int max_cycles) {
    int count = 0;
    while (!executor_.is_halted() && count < max_cycles) {
        step();
        count++;
    }
}

void CPU::reset() {
    regs_.reset();
    mem_.reset();

    executor_.reset();

    for (auto &p : dec_peripherals_) {
        p.reset();
    }

    if (!code_.empty()) {
        mem_.load_program(code_, load_address_);
        regs_.set_pc(load_address_);
    }
}

void CPU::trigger_interrupt(int interrupt_id) {
    executor_.trigger_interrupt(interrupt_id);
}
