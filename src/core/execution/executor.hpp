#pragma once
#include "../alu/alu.hpp"
#include "../config.hpp"
#include "../memory/memory.hpp"
#include "../registers/register_file.hpp"
#include "decoder.hpp"

enum class ExecutionState { FETCH, DECODE, EXECUTE_UOPS, DONE };

class Executor {
  public:
    Executor(Config &config, RegisterFile &regs, Memory &mem, ALU &alu);

    void step_instruction();
    void step_uop();
    void reset();

    ExecutionState get_state() const { return state_; }
    const DecodedInstruction &get_current_inst() const { return current_inst_; }
    size_t get_current_uop_index() const { return uop_index_; }
    bool is_halted() const { return halted_; }
    int get_total_cycles() const { return cycles_; }

  private:
    Config &config_;
    RegisterFile &regs_;
    Memory &mem_;
    ALU &alu_;
    Decoder decoder_;

    ExecutionState state_ = ExecutionState::FETCH;
    DecodedInstruction current_inst_;
    size_t uop_index_ = 0;
    int units_fetched_ = 0;
    bool halted_ = false;
    int cycles_ = 0;
    uint64_t fetch_pc_ = 0;
    uint64_t fetched_bits_;
    int pc_idx_;
    int sp_idx_;
    int flags_idx_;
    uint64_t raw_inst_bits_ = 0;
    int total_bits_to_decode_ = 0;

    void perform_uop(const MicroOp &uop);
    uint64_t resolve_operand(const std::string &arg);
    void write_operand(const std::string &arg, uint64_t value);
    int get_operand_width(const std::string &arg);
};
