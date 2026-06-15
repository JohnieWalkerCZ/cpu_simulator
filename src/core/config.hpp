#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct RegisterDef {
    std::string name;
    int width;
    uint64_t initial;
    std::string role;

    bool is_alias = false;
    int physical_index = -1;
    int parent_index = -1;
    int absolute_bit_offset = 0;

    std::vector<int> bit_mapping;

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
    uint64_t initial;
    nlohmann::json on_read;
    nlohmann::json on_write;
};

struct PeripheralDef {
    std::string name;
    std::string type;
    uint64_t address_start;
    uint64_t address_end;
    std::unordered_map<std::string, std::string> parameters;

    std::vector<PeripheralRegisterDef> registers;
    std::unordered_map<std::string, uint64_t> internal_state;
    nlohmann::json tick_behavior;
};

struct MemorySegmentDef {
    std::string name;
    uint64_t start;
    uint64_t end;
    bool r, w, x;
};

struct Config {
    std::string name;
    int data_width;
    int addr_width;
    int memory_size;
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
