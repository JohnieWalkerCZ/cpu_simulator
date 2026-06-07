#include "core/config.hpp"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
    try {
        // Parse a mock JSON configuration with memory segments and nested
        // registers
        nlohmann::json j = nlohmann::json::parse(R"({
            "name": "TestISA",
            "data_bus": { "width": 8 },
            "address_bus": { "width": 16 },
            "memory": {
                "size": 1024,
                "segments": [
                    {"name": "ROM", "start": "0x0000", "end": "0x00FF", "R": true, "W": false, "X": true},
                    {"name": "RAM", "start": "0x0100", "end": "0x03FF", "R": true, "W": true, "X": false}
                ]
            },
            "registers": {
                "general_purpose": [
                    {
                        "name": "R0", "width": 8, "initial": 0,
                        "sub_registers": [
                            {
                                "name": "R0H", "width": 4, "offset": 4,
                                "sub_registers": [
                                    {"name": "R0H_even", "width": 2, "mask": "0b01"}
                                ]
                            }
                        ]
                    }
                ],
                "special": [
                    {"name": "PC", "width": 16, "initial": 0, "role": "program_counter"}
                ]
            }
        })");

        Config cfg = Config::from_json(j);
        assert(cfg.name == "TestISA");
        assert(cfg.memory_segments.size() == 2);
        assert(cfg.memory_segments[0].name == "ROM");
        assert(!cfg.memory_segments[0].w); // ROM is write-protected

        // Verify registers flattening
        assert(cfg.registers.size() == 4); // R0, R0H, R0H_even, PC

        // R0 (Physical register)
        assert(cfg.registers[0].name == "R0");
        assert(!cfg.registers[0].is_alias);
        assert(cfg.registers[0].physical_index == 0);

        // R0H (Sub-register offset slice)
        assert(cfg.registers[1].name == "R0H");
        assert(cfg.registers[1].is_alias);
        assert(cfg.registers[1].physical_index == 0);
        assert(cfg.registers[1].bit_mapping.size() == 4);
        assert(cfg.registers[1].bit_mapping[0] == 4); // Offset 4

        // R0H_even (Nested mask)
        assert(cfg.registers[2].name == "R0H_even");
        assert(cfg.registers[2].is_alias);
        assert(cfg.registers[2].physical_index == 0);
        assert(cfg.registers[2].bit_mapping.size() == 2);
        assert(cfg.registers[2].bit_mapping[0] == 4);
        assert(cfg.registers[2].bit_mapping[1] == 6);

        std::cout << "Config parser unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
