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

    void step_uop();
    void reset();

    ExecutionState get_state() const { return state_; }
    const DecodedInstruction &get_current_inst() const { return current_inst_; }
    size_t get_current_uop_index() const { return uop_index_; }
    bool is_halted() const { return halted_; }
    int get_total_cycles() const { return cycles_; }
    int get_current_uop_cycles() const { return current_uop_cycles_; }
    int get_current_uop_latency() const;
    void trigger_interrupt(int interrupt_id) {
        interrupt_pending_ = true;
        pending_interrupt_id_ = interrupt_id;
    }
    int get_current_execution_cycles() const {
        return current_execution_cycles_;
    }
    int get_units_fetched() const { return units_fetched_; }

    struct ExecutorSnapshot {
        ExecutionState state;
        DecodedInstruction current_inst;
        size_t uop_index;
        int units_fetched;
        bool halted;
        int cycles;
        uint64_t fetch_pc;
        uint64_t fetched_bits;
        uint64_t raw_inst_bits;
        int total_bits_to_decode;
        int current_uop_cycles;
        bool interrupt_pending;
        int pending_interrupt_id;
        bool pc_modified;
        int current_execution_cycles;
        uint64_t last_data_val;
        uint64_t last_addr_val;
        uint64_t last_alu_a;
        uint64_t last_alu_b;
        uint64_t last_alu_out;
    };

    ExecutorSnapshot take_snapshot() const {
        return {state_,
                current_inst_,
                uop_index_,
                units_fetched_,
                halted_,
                cycles_,
                fetch_pc_,
                fetched_bits_,
                raw_inst_bits_,
                total_bits_to_decode_,
                current_uop_cycles_,
                interrupt_pending_,
                pending_interrupt_id_,
                pc_modified_,
                current_execution_cycles_,
                last_data_val_,
                last_addr_val_,
                last_alu_a_,
                last_alu_b_,
                last_alu_out_};
    }

    void restore_snapshot(const ExecutorSnapshot &snap) {
        state_ = snap.state;
        current_inst_ = snap.current_inst;
        uop_index_ = snap.uop_index;
        units_fetched_ = snap.units_fetched;
        halted_ = snap.halted;
        cycles_ = snap.cycles;
        fetch_pc_ = snap.fetch_pc;
        fetched_bits_ = snap.fetched_bits;
        raw_inst_bits_ = snap.raw_inst_bits;
        total_bits_to_decode_ = snap.total_bits_to_decode;
        current_uop_cycles_ = snap.current_uop_cycles;
        interrupt_pending_ = snap.interrupt_pending;
        pending_interrupt_id_ = snap.pending_interrupt_id;
        pc_modified_ = snap.pc_modified;
        current_execution_cycles_ = snap.current_execution_cycles;
        last_data_val_ = snap.last_data_val;
        last_addr_val_ = snap.last_addr_val;
        last_alu_a_ = snap.last_alu_a;
        last_alu_b_ = snap.last_alu_b;
        last_alu_out_ = snap.last_alu_out;
    }

    uint64_t get_last_data_val() const { return last_data_val_; }
    uint64_t get_last_addr_val() const { return last_addr_val_; }
    uint64_t get_last_alu_a() const { return last_alu_a_; }
    uint64_t get_last_alu_b() const { return last_alu_b_; }
    uint64_t get_last_alu_out() const { return last_alu_out_; }

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
    int current_uop_cycles_ = 0;
    bool interrupt_pending_ = false;
    int pending_interrupt_id_ = -1;
    bool pc_modified_ = false;
    int current_execution_cycles_ = 0;

    // Captured diagnostics registers
    uint64_t last_data_val_ = 0;
    uint64_t last_addr_val_ = 0;
    uint64_t last_alu_a_ = 0;
    uint64_t last_alu_b_ = 0;
    uint64_t last_alu_out_ = 0;

    void perform_uop(const MicroOp &uop);
    void pre_resolve_diagnostics();
    uint64_t resolve_operand(const std::string &arg);
    void write_operand(const std::string &arg, uint64_t value);
    int get_operand_width(const std::string &arg);
};
