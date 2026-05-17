#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct RegisterDef {
    std::string name;
    int width; // bits
    uint64_t initial;
    std::string role; // "", "pc", "sp", "flags"
};

struct ALUOp {
    std::string name;
    uint8_t code;
    std::string format; // "RR", "RI", "R"
    int latency;
};

struct Instruction {
    std::string name;
    uint8_t opcode;
    std::string format;
    std::vector<int> encoding;
};

struct Config {
    std::string name;
    int data_width; // 4, 8, 16, 32
    int addr_width;
    int memory_size;

    std::vector<RegisterDef> registers;
    std::vector<ALUOp> alu_ops;
    std::vector<Instruction> instructions;

    bool validate() const;
    std::string get_error() const;

    static Config from_json(const nlohmann::json &j);
    static Config from_file(const std::string &path);
};
