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

        // Non-power-of-two, >64-bit widths: data_width=12, addr_width=96,
        // and a register wider than 64 bits with an initial value supplied
        // as a hex string (JSON numbers can't carry full word_t precision).
        {
            nlohmann::json wide_j = nlohmann::json::parse(R"({
                "name": "WideISA",
                "data_bus": { "width": 12 },
                "address_bus": { "width": 96 },
                "memory": { "size": 4096 },
                "registers": {
                    "general_purpose": [
                        {"name": "W0", "width": 100,
                         "initial": "0xFFFFFFFFFFFFFFFFFFFFFFFF"}
                    ],
                    "special": [
                        {"name": "PC", "width": 96, "initial": 0,
                         "role": "program_counter"}
                    ]
                }
            })");

            Config wide_cfg = Config::from_json(wide_j);
            assert(wide_cfg.data_width == 12);
            assert(wide_cfg.addr_width == 96);
            assert(wide_cfg.registers[0].width == 100);
            assert(wide_cfg.registers[0].initial ==
                  parse_word("0xFFFFFFFFFFFFFFFFFFFFFFFF"));
            assert(wide_cfg.validate());
        }

        // validate() must still reject widths that aren't a multiple of 4,
        // and widths beyond the 128-bit ceiling.
        {
            Config bad_cfg = Config::from_json(nlohmann::json::parse(R"({
                "name": "BadWidth",
                "data_bus": { "width": 10 },
                "address_bus": { "width": 16 },
                "memory": { "size": 1024 },
                "registers": {
                    "special": [
                        {"name": "PC", "width": 16, "initial": 0,
                         "role": "program_counter"}
                    ]
                }
            })"));
            assert(!bad_cfg.validate());

            Config too_wide_cfg = Config::from_json(nlohmann::json::parse(R"({
                "name": "TooWide",
                "data_bus": { "width": 8 },
                "address_bus": { "width": 132 },
                "memory": { "size": 1024 },
                "registers": {
                    "special": [
                        {"name": "PC", "width": 16, "initial": 0,
                         "role": "program_counter"}
                    ]
                }
            })"));
            assert(!too_wide_cfg.validate());
        }

        std::cout << "Config parser unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
