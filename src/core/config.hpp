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

struct Instruction {
    std::string name;
    uint8_t opcode;
    std::string format;
    std::vector<int> encoding;
    std::vector<MicroOp> microcode;
};

struct FlagDef {
    std::string name;
    int bit;
    std::string type; // "zero", "carry", "overflow", "negative", etc.
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
    uint32_t address_start;
    uint32_t address_end;
    std::unordered_map<std::string, std::string> parameters;

    std::vector<PeripheralRegisterDef> registers;
    std::unordered_map<std::string, uint64_t> internal_state;
    nlohmann::json tick_behavior;
};

struct MemorySegmentDef {
    std::string name;
    uint32_t start;
    uint32_t end;
    bool r, w, x;
};

struct Config {
    std::string name;
    int data_width;
    int addr_width;
    int memory_size;

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
