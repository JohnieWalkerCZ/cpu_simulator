#pragma once
#include "wide_int.hpp"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct RegisterDef {
    std::string name;
    int width;
    word_t initial;
    std::string role;

    bool is_alias = false;
    int physical_index = -1;
    int parent_index = -1;
    int absolute_bit_offset = 0;

    std::vector<int> bit_mapping;

    // Precomputed by RegisterFile's constructor (not by the config loader):
    // true when bit_mapping is a contiguous run of physical bits, letting
    // read/write use a single shift-and-mask instead of looping bit by bit.
    bool is_contiguous = false;
    word_t bit_mask = 0;
    int bit_shift = 0;

    bool is_coproc = false;
    int coproc_id = -1;
    int coproc_reg_id = -1;
};

struct ALUOp {
    std::string name;
    uint8_t code;
    std::string expression;
    std::unordered_map<std::string, std::string> flag_rules;
    int latency;
};

struct MicroOp {
    std::string
        action; // "copy", "alu", "mem_read", "mem_write", "branch", "halt"
    std::unordered_map<std::string, std::string> args;
};

enum class LatencyMode {
    DYNAMIC,
    BOTTLENECK,
    STRICT,
    ADDITIVE,
};

struct Instruction {
    std::string name;
    uint8_t opcode;
    std::vector<int> encoding;
    std::vector<MicroOp> microcode;
    int execution_latency = -1;
    LatencyMode latency_mode = LatencyMode::DYNAMIC;
};

struct FlagDef {
    std::string name;
    int bit;
    std::string type; // "zero", "carry", "overflow", "negative", etc.
    std::string expression = "";
};

struct PeripheralRegisterDef {
    std::string name;
    int offset;
    int size_bytes;
    std::string access;
    word_t initial;
    nlohmann::json on_read;
    nlohmann::json on_write;
};

struct PeripheralDef {
    std::string name;
    std::string type;
    word_t address_start;
    word_t address_end;
    std::unordered_map<std::string, std::string> parameters;

    std::vector<PeripheralRegisterDef> registers;
    std::unordered_map<std::string, word_t> internal_state;
    nlohmann::json tick_behavior;
};

struct MemorySegmentDef {
    std::string name;
    word_t start;
    word_t end;
    bool r, w, x;
};

struct Config {
    std::string name;
    int data_width;
    int addr_width;
    word_t memory_size;
    std::string endianness = "little";
    std::string memory_architecture = "von_neumann";

    std::vector<RegisterDef> registers;
    std::vector<FlagDef> alu_flags;
    std::vector<ALUOp> alu_ops;
    std::vector<Instruction> instructions;
    std::vector<PeripheralDef> peripherals;
    std::vector<MemorySegmentDef> memory_segments;

    bool validate() const;
    std::string get_error() const;

    static Config from_json(const nlohmann::json &j);
    static Config from_file(const std::string &path);
};
